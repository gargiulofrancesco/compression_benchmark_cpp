#include "onpair_compressor.h"
#include <cstring>
#include <robin_hood.h>
#include <random>
#include <numeric>

OnPairCompressor::OnPairCompressor(size_t data_size, size_t n_elements) {
    compressed_data.reserve(data_size);
    string_boundaries.reserve(n_elements + 1);
    dictionary.reserve(1024 * 1024);
    token_boundaries.reserve(1 << 16);
}

void OnPairCompressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    std::vector<int> shuffled_indices = generate_random_permutation(end_positions.size() - 1);
    auto [lpm, _] = train_dictionary(data, end_positions, THRESHOLD, shuffled_indices);
    parse_data(data, end_positions, lpm);
}

// Assumes buffer has enough space to store the decompressed data
size_t OnPairCompressor::decompress(uint8_t* buffer) const {
    const uint8_t* dict_ptr = dictionary.data();
    const uint32_t* offsets_ptr = token_boundaries.data();
    size_t size = 0;

    for (uint16_t token_id : compressed_data) {
        size_t dict_start = offsets_ptr[token_id];
        size_t dict_end = offsets_ptr[token_id + 1];
        size_t length = dict_end - dict_start;

        std::memcpy(buffer + size, dict_ptr + dict_start, FAST_COPY_SIZE);
        if(length > FAST_COPY_SIZE) {
            std::memcpy(buffer + size + FAST_COPY_SIZE, dict_ptr + dict_start + FAST_COPY_SIZE, length - FAST_COPY_SIZE);
        }

        size += length;
    }

    return size;
}

// Assumes buffer has enough space to store the decompressed data
size_t OnPairCompressor::get_item_at(size_t index, uint8_t* buffer) const {
    const uint8_t* dict_ptr = dictionary.data();
    const uint32_t* offsets_ptr = token_boundaries.data();
    size_t size = 0;

    size_t data_start = string_boundaries[index];
    size_t data_end = string_boundaries[index + 1];

    for (size_t i = data_start; i < data_end; i++) {
        uint16_t token_id = compressed_data[i];

        size_t dict_start = offsets_ptr[token_id];
        size_t dict_end = offsets_ptr[token_id + 1];
        size_t length = dict_end - dict_start;

        // Copy the dictionary entry to the buffer
        std::memcpy(buffer + size, dict_ptr + dict_start, FAST_COPY_SIZE);
        if(length > FAST_COPY_SIZE) {
            std::memcpy(buffer + size + FAST_COPY_SIZE, dict_ptr + dict_start + FAST_COPY_SIZE, length - FAST_COPY_SIZE);
        }

        size += length;
    }

    return size;
}

size_t OnPairCompressor::space_used_bytes() const {
    return compressed_data.size() * sizeof(uint16_t) + dictionary.size() + token_boundaries.size() * sizeof(uint32_t);
}

const char* OnPairCompressor::name() const {
    return "OnPair";
}

std::vector<int> OnPairCompressor::generate_random_permutation(const size_t n_elements, const size_t seed) {
    std::vector<int> shuffled_indices(n_elements);
    std::iota(shuffled_indices.begin(), shuffled_indices.end(), 0);

    std::mt19937 g(seed);
    std::shuffle(shuffled_indices.begin(), shuffled_indices.end(), g);

    return shuffled_indices;
}

std::pair<LongestPrefixMatcher<uint16_t>, size_t> OnPairCompressor::train_dictionary(const uint8_t* data, const std::vector<size_t>& end_positions, const size_t threshold, const std::vector<int>& shuffled_indices) {
    token_boundaries.push_back(0);
    
    robin_hood::unordered_map<std::pair<uint16_t, uint16_t>, uint16_t, PairHash> frequency;
    LongestPrefixMatcher<uint16_t> lpm;
    uint16_t next_token_id = 256;
    bool full_dictionary = false;
    size_t sample_size_bytes = 0;

    // Initialize the dictionary with single-byte tokens
    for(uint16_t i=0; i<=255; i++) {
        uint8_t value = static_cast<uint8_t>(i);
        lpm.insert(&value, 1, i);
        dictionary.push_back(value);
        token_boundaries.push_back(dictionary.size());
    }

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
        uint16_t previous_token_id = match.value().first;
        size_t previous_length = match.value().second; 

        sample_size_bytes += previous_length;
        size_t pos = start + previous_length;

        while (pos < end) {
            // Find the longest match
            auto match = lpm.find_longest_match(data + pos, end - pos);
            uint16_t match_token_id = match.value().first;
            size_t match_length = match.value().second;
            sample_size_bytes += match_length;

            // Update token frequency and possibly merge tokens
            auto token_pair = std::make_pair(previous_token_id, match_token_id);
            frequency[token_pair]++;

            if (frequency[token_pair] >= threshold) {
                lpm.insert(data + pos - previous_length, previous_length + match_length, next_token_id);
                dictionary.insert(dictionary.end(), data + pos - previous_length, data + pos + match_length);
                token_boundaries.push_back(dictionary.size());
                
                frequency.erase(token_pair);
                previous_token_id = next_token_id;
                previous_length += match_length;

                if (next_token_id == std::numeric_limits<uint16_t>::max()) {
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

    return {std::move(lpm), sample_size_bytes};
}

void OnPairCompressor::parse_data(const uint8_t* data, const std::vector<size_t>& end_positions, const LongestPrefixMatcher<uint16_t>& lpm) {
    string_boundaries.push_back(0);

    for(int i=0; i<end_positions.size()-1; i++) {
        size_t start = end_positions[i];
        size_t end = end_positions[i+1];

        if (start == end) {
            string_boundaries.push_back(compressed_data.size());
            continue;
        }

        size_t pos = start;
        while (pos < end) {
            // Find the longest match
            auto match = lpm.find_longest_match(data + pos, end - pos);
            uint16_t token_id = match->first;
            size_t length = match->second;
            compressed_data.push_back(token_id);
            pos += length;
        }

        string_boundaries.push_back(compressed_data.size());
    }
}