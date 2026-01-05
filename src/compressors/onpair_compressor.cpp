#include "onpair_compressor.h"
#include <cstring>
#include <robin_hood.h>
#include <random>
#include <numeric>
#include <algorithm>

OnPairCompressor::OnPairCompressor(size_t data_size, size_t n_elements) {
    compressed_data.reserve(data_size);
    string_boundaries.reserve(n_elements + 1);
    dictionary.reserve(1024 * 1024);
    token_boundaries.reserve(1 << 16);
    threshold = 10; // Default threshold
    seed = 42;    // Default seed
}

void OnPairCompressor::set_threshold(size_t threshold) {
    this->threshold = threshold;
}

void OnPairCompressor::set_seed(size_t seed) {
    this->seed = seed;
}

size_t OnPairCompressor::get_threshold() const {
    return threshold;
}

size_t OnPairCompressor::get_seed() const {
    return seed;
}

void OnPairCompressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    std::vector<int> shuffled_indices = generate_random_permutation(end_positions.size() - 1);
    auto [lpm, _] = train_dictionary(data, end_positions, shuffled_indices);
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

std::pair<LongestPrefixMatcher<uint16_t>, size_t> OnPairCompressor::train_dictionary(const uint8_t* data, const std::vector<size_t>& end_positions, const std::vector<int>& shuffled_indices) {
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

std::vector<int> OnPairCompressor::generate_random_permutation(const size_t n_elements) {
    std::vector<int> shuffled_indices(n_elements);
    std::iota(shuffled_indices.begin(), shuffled_indices.end(), 0);

    std::mt19937 g(seed);
    std::shuffle(shuffled_indices.begin(), shuffled_indices.end(), g);

    return shuffled_indices;
}

LongestPrefixMatcher<uint16_t> OnPairCompressor::sort_dictionary() {
    std::vector<std::vector<uint8_t>> tokens;
    size_t num_tokens = token_boundaries.size() - 1;
    tokens.reserve(num_tokens);

    for (size_t i = 0; i < num_tokens; ++i) {
        size_t start = token_boundaries[i];
        size_t end = token_boundaries[i+1];
        tokens.emplace_back(dictionary.begin() + start, dictionary.begin() + end);
    }

    std::sort(tokens.begin(), tokens.end());

    // Rebuild dictionary and LPM
    dictionary.clear();
    token_boundaries.clear();
    token_boundaries.push_back(0);
    
    LongestPrefixMatcher<uint16_t> lpm;
    
    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        dictionary.insert(dictionary.end(), token.begin(), token.end());
        token_boundaries.push_back(dictionary.size());
        lpm.insert(token.data(), token.size(), static_cast<uint16_t>(i));
    }

    return std::move(lpm);
}

uint32_t OnPairCompressor::lower_bound(const std::vector<uint8_t>& target) const {
    size_t left = 0;
    size_t right = token_boundaries.size() - 1; // Number of tokens
    
    std::string_view target_sv(reinterpret_cast<const char*>(target.data()), target.size());

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        
        // Reconstruct token string_view
        size_t start = token_boundaries[mid];
        size_t len = token_boundaries[mid+1] - start;
        std::string_view token_sv(reinterpret_cast<const char*>(dictionary.data() + start), len);
        
        if (token_sv < target_sv) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return static_cast<uint32_t>(left);
}

// Warning: Requires the underlying dictionary to be sorted lexicographically
size_t OnPairCompressor::prefix_search(const LongestPrefixMatcher<uint16_t>& lpm, const std::vector<uint8_t>& prefix, size_t* buffer) const {    
    // 1. Parse prefix into tokens
    std::vector<uint16_t> query_tokens;
    size_t pos = 0;
    while (pos < prefix.size()) {
        auto match = lpm.find_longest_match(prefix.data() + pos, prefix.size() - pos);
        query_tokens.push_back(match->first);
        pos += match->second;
    }

    size_t q_len = query_tokens.size();

    // 2. Precompute intervals
    std::vector<std::pair<uint32_t, uint32_t>> intervals;
    intervals.reserve(q_len);
    
    size_t current_pos = 0;
    std::vector<uint8_t> temp_suffix;
    temp_suffix.reserve(prefix.size());
    for (size_t i = 0; i < q_len; ++i) {
        // Reconstruct suffix: prefix[current_pos...]
       temp_suffix.assign(prefix.begin() + current_pos, prefix.end());
        
        // Find Lower Bound (Start of range)
        uint32_t start_idx = lower_bound(temp_suffix);
        
        // Find Upper Bound (Start of next lexical prefix)
        // Logic: Increment the last byte. If it overflows (255), pop and carry.
        bool valid_next = false;
        while (!temp_suffix.empty()) {
            if (temp_suffix.back() < 255) {
                temp_suffix.back()++;
                valid_next = true;
                break;
            } else {
                temp_suffix.pop_back();
            }
        }
        
        uint32_t end_idx;
        if (valid_next) {
            end_idx = lower_bound(temp_suffix);
        } else {
            end_idx = static_cast<uint32_t>(token_boundaries.size() - 1);
        }
        
        intervals.emplace_back(start_idx, end_idx);
        
        // Advance position by length of current token
        size_t t_start = token_boundaries[query_tokens[i]];
        size_t t_end = token_boundaries[query_tokens[i]+1];
        current_pos += (t_end - t_start);
    }

    // 3. Scan
    size_t match_count = 0;
    size_t n_items = string_boundaries.size() - 1;

    const uint16_t* compressed_ptr = compressed_data.data();
    const size_t* boundaries_ptr = string_boundaries.data();
    
    for (size_t i = 0; i < n_items; ++i) {
        size_t start = boundaries_ptr[i];
        size_t end = boundaries_ptr[i+1];
        size_t item_len = end - start;

        bool match_found = true;

        // Prefix scan
        for (size_t j = 0; j < q_len; ++j) {
            if (j >= item_len) {
                match_found = false;
                break;
            }

            const uint16_t item_token = compressed_ptr[start + j];
            const uint16_t q_token = query_tokens[j];

            if (item_token == q_token) {
                continue;
            }

            // Divergence: interval check
            const auto& interval = intervals[j];
            if (item_token >= interval.first && item_token < interval.second) {
                // Valid prefix match via interval
                break;
            }

            // Hard mismatch
            match_found = false;
            break;
        }

        if (match_found) {
            buffer[match_count++] = i;
        }
    }
    
    return match_count;
}