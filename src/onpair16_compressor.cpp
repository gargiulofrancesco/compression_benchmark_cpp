#include "onpair16_compressor.h"
#include <cstring>
#include <robin_hood.h>


#include <chrono>
#include <sched.h>
#include <iostream>



OnPair16Compressor::OnPair16Compressor(size_t data_size, size_t n_elements) {
    compressed_data.reserve(data_size);
    offsets.reserve(n_elements);
    dictionary_data.reserve(2 * (1024 * 1024)); // 2 MB
    dictionary_offsets.reserve(1 << 16);
}

void OnPair16Compressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    LongestPrefixMatcher16<uint16_t> lpm;
    auto start_training = std::chrono::high_resolution_clock::now();
    lpm = train(data, end_positions);
    auto end_training = std::chrono::high_resolution_clock::now();
    double training_time = duration_cast<std::chrono::duration<double>>(end_training - start_training).count();
    double training_speed = (static_cast<double>(end_positions.back()) / (1024.0 * 1024.0)) / training_time;
    std::cout << "training time: " << training_time << ", training speed: " << training_speed << std::endl;

    auto start_parsing = std::chrono::high_resolution_clock::now();
    parse(data, end_positions, lpm);
    auto end_parsing = std::chrono::high_resolution_clock::now();
    double parsing_time = duration_cast<std::chrono::duration<double>>(end_parsing - start_parsing).count();
    double parsing_speed = (static_cast<double>(end_positions.back()) / (1024.0 * 1024.0)) / parsing_time;
    std::cout << "parsing time: " << parsing_time << ", parsing speed: " << parsing_speed << std::endl << std::endl;
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

        __builtin_memcpy(buffer + size, dict_ptr + dict_start, MAX_LENGTH);
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
        __builtin_memcpy(buffer + size, dict_ptr + dict_start, MAX_LENGTH);
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
    robin_hood::unordered_map<std::pair<uint16_t, uint16_t>, size_t, FastPairHash> frequency;
    LongestPrefixMatcher16<uint16_t> lpm;   
    uint16_t next_token_id = 256;

    // Initialize the dictionary with single-byte tokens
    for(uint16_t i=0; i<=255; i++) {
        uint8_t value = static_cast<uint8_t>(i);
        lpm.insert(&value, 1, i);
    }

    size_t start = 0;
    size_t pos = 0;

    // Iterate over end positions
    for (size_t end : end_positions) {
        if (next_token_id == 65535) {
            std::cout << "dictionary full at " << pos << "/" << end_positions.back() << " (" << (static_cast<double>(pos)/static_cast<double>(end_positions.back())) * 100.0 <<  ")" <<std::endl;
            break; 
        }

        if (start == end) {
            continue;
        }

        auto match = lpm.find_longest_match(data + pos, end - pos);
        uint16_t previous_token_id = match.value().first;
        size_t previous_length = match.value().second; 

        pos = start + previous_length;

        while (pos < end) {
            if (next_token_id == 65535) {
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
                    next_token_id++;
                    frequency.erase(token_pair);
                }
            }

            previous_token_id = match_token_id;
            previous_length = match_length;
            pos += match_length;
        }

        start = end;
    }

    return std::move(lpm);
}

void OnPair16Compressor::parse(const uint8_t* data, const std::vector<size_t>& end_positions, const LongestPrefixMatcher16<uint16_t>& lpm) {
    dictionary_offsets.push_back(0);
    offsets.push_back(0);

    // Initialize dictionary_map as a vector of std::optional<uint16_t>
    std::vector<uint16_t> dictionary_map(1 << 16, 0xFFFF);  // Use 0xFFFF as sentinel value
    uint16_t next_token_id = 0;

    size_t start = 0;
    for (size_t end : end_positions) {
        if (start == end) {
            offsets.push_back(compressed_data.size());
            continue;
        }

        size_t pos = start;
        while (pos < end) {
            // Find the longest match
            auto match = lpm.find_longest_match(data + pos, end - pos);
            uint16_t match_token_id = match->first;
            size_t match_length = match->second;

            // Check if the token is already in the dictionary_map
            uint16_t& existing_token = dictionary_map[match_token_id];
            if (existing_token != 0xFFFF) {
                // Token exists, use it directly
                compressed_data.push_back(existing_token);
            } else {
                // Create new token
                dictionary_map[match_token_id] = next_token_id;
                compressed_data.push_back(next_token_id);

                 // Add the new token to the dictionary
                dictionary_data.insert(dictionary_data.end(), data + pos, data + pos + match_length);
                dictionary_offsets.push_back(dictionary_data.size());

                next_token_id++;
            }

            pos += match_length;
        }

        offsets.push_back(compressed_data.size());
        start = end;
    }
}