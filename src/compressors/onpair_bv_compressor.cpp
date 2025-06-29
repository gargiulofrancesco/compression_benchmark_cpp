#include "onpair_bv_compressor.h"
#include <cstring>
#include <robin_hood.h>
#include <random>
#include <queue>
#include <iostream>

OnPairBVCompressor::OnPairBVCompressor(size_t data_size, size_t n_elements) {
    compressed_data.with_capacity(data_size * BITS_PER_TOKEN);
    offsets.reserve(n_elements);
    dictionary_data.reserve(2 * 1024 * 1024); // 2 MiB
    dictionary_offsets.reserve(1 << 16);
}

void OnPairBVCompressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    LongestPrefixMatcher<uint32_t> lpm = train(data, end_positions);
    parse(data, end_positions, lpm);
}

// Assumes buffer has enough space to store the decompressed data
size_t OnPairBVCompressor::decompress(uint8_t* buffer) const {
    const uint8_t* dict_ptr = dictionary_data.data();
    const uint32_t* offsets_ptr = dictionary_offsets.data();
    size_t size = 0;

    for(size_t i=0; i < compressed_data.len() / BITS_PER_TOKEN; i++) {
        size_t offset = i * BITS_PER_TOKEN;
        uint32_t token_id = compressed_data.get_bits(offset, BITS_PER_TOKEN).value();

        size_t dict_start = offsets_ptr[token_id];
        size_t dict_end = offsets_ptr[token_id + 1];
        size_t length = dict_end - dict_start;

        std::memcpy(buffer + size, dict_ptr + dict_start, FAST_ACCESS_SIZE);
        if(length > FAST_ACCESS_SIZE) {
            std::memcpy(buffer + size + FAST_ACCESS_SIZE, dict_ptr + dict_start + FAST_ACCESS_SIZE, length - FAST_ACCESS_SIZE);
        }

        size += length;
    }

    return size;
}

// Assumes buffer has enough space to store the decompressed data
size_t OnPairBVCompressor::get_item_at(size_t index, uint8_t* buffer) const {
    size_t item_start = offsets[index];
    size_t item_end = offsets[index + 1];

    const uint8_t* dict_ptr = dictionary_data.data();
    const uint32_t* offsets_ptr = dictionary_offsets.data();
    size_t size = 0;

    for (size_t i = item_start; i < item_end; i++) {
        size_t offset = i * BITS_PER_TOKEN;
        uint32_t token_id = compressed_data.get_bits(offset, BITS_PER_TOKEN).value();

        size_t dict_start = offsets_ptr[token_id];
        size_t dict_end = offsets_ptr[token_id + 1];
        size_t length = dict_end - dict_start;

        // Copy the dictionary entry to the buffer
        std::memcpy(buffer + size, dict_ptr + dict_start, FAST_ACCESS_SIZE);
        if(length > FAST_ACCESS_SIZE) {
            std::memcpy(buffer + size + FAST_ACCESS_SIZE, dict_ptr + dict_start + FAST_ACCESS_SIZE, length - FAST_ACCESS_SIZE);
        }

        size += length;
    }

    return size;
}

size_t OnPairBVCompressor::space_used_bytes() const {
    return (compressed_data.len() / 8) + dictionary_data.size() + dictionary_offsets.size() * sizeof(uint32_t);
}

const char* OnPairBVCompressor::name() const {
    return "OnPair BV";
}

LongestPrefixMatcher<uint32_t> OnPairBVCompressor::train(const uint8_t* data, const std::vector<size_t>& end_positions) {
    dictionary_offsets.push_back(0);
    
    robin_hood::unordered_map<std::pair<uint32_t, uint32_t>, uint16_t, pair_hash> frequency;
    LongestPrefixMatcher<uint32_t> lpm;
    uint32_t next_token_id = 256;
    bool full_dictionary = false;

    // Initialize the dictionary with single-byte tokens
    for(uint32_t i=0; i<=255; i++) {
        uint8_t value = static_cast<uint8_t>(i);
        lpm.insert(&value, 1, i);
        dictionary_data.push_back(value);
        dictionary_offsets.push_back(dictionary_data.size());
    }

    // Shuffle entries
    std::vector<int> shuffled_indices;
    for (int i=0; i<end_positions.size()-1; i++) {
        shuffled_indices.push_back(i);
    }
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(shuffled_indices.begin(), shuffled_indices.end(), g);

    // Set the threshold for merging tokens
    double data_size_mib = static_cast<double>(end_positions.back()) / (1024.0 * 1024.0);
    size_t threshold = static_cast<size_t>(std::fmax(std::log2(data_size_mib), 2.0));

    // Iterate over entries
    for(auto index : shuffled_indices){ 
        size_t start = end_positions[index];
        size_t end = end_positions[index+1];

        if (full_dictionary) {
            break; 
        }

        if (start == end) {
            continue;
        }

        auto match = lpm.find_longest_match(data + start, end - start);
        uint32_t previous_token_id = match.value().first;
        size_t previous_length = match.value().second; 

        size_t pos = start + previous_length;

        while (pos < end) {
            // Find the longest match
            auto match = lpm.find_longest_match(data + pos, end - pos);
            uint32_t match_token_id = match.value().first;
            size_t match_length = match.value().second; 

            // Update token frequency and possibly merge tokens
            auto token_pair = std::make_pair(previous_token_id, match_token_id);
            frequency[token_pair]++;

            if (frequency[token_pair] >= threshold) {
                lpm.insert(data + pos - previous_length, previous_length + match_length, next_token_id);
                dictionary_data.insert(dictionary_data.end(), data + pos - previous_length, data + pos + match_length);
                dictionary_offsets.push_back(dictionary_data.size());
                
                frequency.erase(token_pair);
                previous_token_id = next_token_id;
                previous_length += match_length;

                if (next_token_id == MAX_TOKEN_ID) {
                    full_dictionary = true;
                    break;
                }
                
                next_token_id++;
            }
            else {
                previous_token_id = match_token_id;
                previous_length = match_length;
            }

            pos += match_length;
        }
    }

    return std::move(lpm);
}

void OnPairBVCompressor::parse(const uint8_t* data, const std::vector<size_t>& end_positions, const LongestPrefixMatcher<uint32_t>& lpm) {
    offsets.push_back(0);

    for(int i=0; i<end_positions.size()-1; i++) {
        size_t start = end_positions[i];
        size_t end = end_positions[i+1];

        if (start == end) {
            offsets.push_back(compressed_data.len() / BITS_PER_TOKEN);
            continue;
        }

        size_t pos = start;
        while (pos < end) {
            // Find the longest match
            auto match = lpm.find_longest_match(data + pos, end - pos);
            uint32_t token_id = match->first;
            size_t length = match->second;
            compressed_data.append_bits(token_id, BITS_PER_TOKEN);
            pos += length;
        }

        offsets.push_back(compressed_data.len() / BITS_PER_TOKEN);
    }
}
