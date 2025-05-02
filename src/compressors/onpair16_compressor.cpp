#include "onpair16_compressor.h"
#include <cstring>
#include <robin_hood.h>
#include <random>

OnPair16Compressor::OnPair16Compressor(size_t data_size, size_t n_elements) {
    compressed_data.reserve(data_size);
    offsets.reserve(n_elements);
    dictionary_data.reserve(2 * (1024 * 1024)); // 2 MB
    dictionary_offsets.reserve(1 << 16);
}

void OnPair16Compressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    auto [sampled_data, sampled_end_positions] = sampling(data, end_positions, 32 * 1024 * 1024);
    LongestPrefixMatcher16<uint16_t> lpm = train(sampled_data.data(), sampled_end_positions);
    parse(data, end_positions, lpm);
}

// Assumes buffer has enough space to store the decompressed data
size_t OnPair16Compressor::decompress(uint8_t* buffer) const {
    const uint8_t* dict_ptr = dictionary_data.data();
    const uint32_t* offsets_ptr = dictionary_offsets.data();
    size_t size = 0;

    for (uint16_t token_id : compressed_data) {
        size_t dict_start = offsets_ptr[token_id];
        size_t dict_end = offsets_ptr[token_id + 1];
        size_t length = dict_end - dict_start;

        std::memcpy(buffer + size, dict_ptr + dict_start, MAX_LENGTH);
        size += length;
    }

    return size;
}

// Assumes buffer has enough space to store the decompressed data
size_t OnPair16Compressor::get_item_at(size_t index, uint8_t* buffer) const {
    const uint8_t* dict_ptr = dictionary_data.data();
    const uint32_t* offsets_ptr = dictionary_offsets.data();
    size_t size = 0;

    size_t data_start = offsets[index];
    size_t data_end = offsets[index + 1];

    for (size_t i = data_start; i < data_end; i++) {
        uint16_t token_id = compressed_data[i];

        size_t dict_start = offsets_ptr[token_id];
        size_t dict_end = offsets_ptr[token_id + 1];
        size_t length = dict_end - dict_start;

        // Copy the dictionary entry to the buffer
        std::memcpy(buffer + size, dict_ptr + dict_start, MAX_LENGTH);
        size += length;
    }

    return size;
}

size_t OnPair16Compressor::space_used_bytes() const {
    return compressed_data.size() * sizeof(uint16_t) + dictionary_data.size() + dictionary_offsets.size() * sizeof(uint32_t);
}

const char* OnPair16Compressor::name() const {
    return "OnPair16";
}

LongestPrefixMatcher16<uint16_t> OnPair16Compressor::train(const uint8_t* data, const std::vector<size_t>& end_positions) {
    dictionary_offsets.push_back(0);
    
    robin_hood::unordered_map<std::pair<uint16_t, uint16_t>, size_t, pair_hash> frequency;
    LongestPrefixMatcher16<uint16_t> lpm;   
    uint16_t next_token_id = 256;

    // Initialize the dictionary with single-byte tokens
    for(uint16_t i=0; i<=255; i++) {
        uint8_t value = static_cast<uint8_t>(i);
        lpm.insert(&value, 1, i);
        dictionary_data.push_back(value);
        dictionary_offsets.push_back(dictionary_data.size());
    }

    // Iterate over entries
    for(int i=0; i<end_positions.size()-1; i++) {
        size_t start = end_positions[i];
        size_t end = end_positions[i+1];

        if (next_token_id == MAX_TOKENS) {
            break; 
        }

        if (start == end) {
            continue;
        }

        auto match = lpm.find_longest_match(data + start, end - start);
        uint16_t previous_token_id = match.value().first;
        size_t previous_length = match.value().second; 

        size_t pos = start + previous_length;

        while (pos < end) {
            if (next_token_id == MAX_TOKENS) {
                break; 
            }

            // Find the longest match
            auto match = lpm.find_longest_match(data + pos, end - pos);
            uint16_t match_token_id = match.value().first;
            size_t match_length = match.value().second; 
            
            if (match_length + previous_length <= MAX_LENGTH) {
                // Update token frequency and possibly merge tokens
                auto token_pair = std::make_pair(previous_token_id, match_token_id);
                frequency[token_pair]++;

                if (frequency[token_pair] > THRESHOLD) {
                    lpm.insert(data + pos - previous_length, previous_length + match_length, next_token_id);
                    dictionary_data.insert(dictionary_data.end(), data + pos - previous_length, data + pos + match_length);
                    dictionary_offsets.push_back(dictionary_data.size());
                    
                    next_token_id++;
                    frequency.erase(token_pair);
                }
            }

            previous_token_id = match_token_id;
            previous_length = match_length;
            pos += match_length;
        }
    }

    return std::move(lpm);
}

void OnPair16Compressor::parse(const uint8_t* data, const std::vector<size_t>& end_positions, const LongestPrefixMatcher16<uint16_t>& lpm) {
    offsets.push_back(0);

    for(int i=0; i<end_positions.size()-1; i++) {
        size_t start = end_positions[i];
        size_t end = end_positions[i+1];

        if (start == end) {
            offsets.push_back(compressed_data.size());
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

        offsets.push_back(compressed_data.size());
    }
}

std::pair<std::vector<uint8_t>, std::vector<size_t>> OnPair16Compressor::sampling(const uint8_t* data, const std::vector<size_t>& end_positions, const size_t sample_size){
    std::vector<uint8_t> sampled_data;
    std::vector<size_t> sampled_end_positions;

    size_t n = end_positions.size() - 1;
    std::vector<size_t> sampled_indices;

    for (size_t i=0; i<n; i++) {
        sampled_indices.push_back(i);
    }

    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(sampled_indices.begin(), sampled_indices.end(), g);

    sampled_end_positions.push_back(0);
    for(size_t i=0; i<n && sampled_data.size()<=sample_size; i++){
        size_t index = sampled_indices[i];
        for(size_t j=end_positions[index]; j<end_positions[index+1]; j++){
            sampled_data.push_back(data[j]);
        }
        sampled_end_positions.push_back(sampled_data.size());
    }

    return {sampled_data, sampled_end_positions};
}