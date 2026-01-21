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

uint32_t OnPairCompressor::lower_bound(const uint8_t* target, size_t target_length) const {
    uint32_t left = 0;
    uint32_t right = token_boundaries.size() - 1;

    const uint8_t* dict_base = dictionary.data();
    const uint32_t* boundaries = token_boundaries.data();

    while (left < right) {
        uint32_t mid = left + ((right - left) >> 1);

        // Compare token[mid] vs target
        size_t token_start = boundaries[mid];
        size_t token_len = boundaries[mid+1] - token_start;
        const uint8_t* token_ptr = dict_base + token_start;

        size_t min_len = (token_len < target_length) ? token_len : target_length;
        int cmp = std::memcmp(token_ptr, target, min_len);

        if (cmp < 0 || (cmp == 0 && token_len < target_length)) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return left;
}

std::pair<uint32_t, uint32_t> OnPairCompressor::valid_divergence(const uint8_t* suffix_ptr, size_t suffix_len, uint8_t* buffer) const {
    uint32_t lb = lower_bound(suffix_ptr, suffix_len);
    
    // Find Upper Bound (Start of next lexical prefix)
    bool overflow = true;
    size_t next_len = 0;
    while(suffix_len > 0) {
        if (suffix_ptr[suffix_len - 1] < 255) {
            overflow = false;
            next_len = suffix_len; // Length of the new prefix
            break;
        }
        suffix_len--;
    }

    uint32_t ub;
    if(overflow) {
        // All bytes were 255 (or empty suffix), so we go to the very end of the dictionary
        ub = static_cast<uint32_t>(token_boundaries.size() - 1);
    } else{
        std::memcpy(buffer, suffix_ptr, next_len);            
        buffer[next_len - 1]++;
        ub = lower_bound(buffer, next_len);
    }

    return {lb, ub};
}

// PRE-CONDITION: Dictionary must be sorted lexicographically
size_t OnPairCompressor::prefix_filtering_sorted(
    const LongestPrefixMatcher<uint16_t>& lpm, 
    const std::vector<uint8_t>& prefix, 
    size_t* buffer
) const {    
    // 1. Parse prefix into tokens
    std::vector<uint16_t> query_tokens;
    query_tokens.reserve(prefix.size()); 

    size_t pos = 0;
    while (pos < prefix.size()) {
        auto match = lpm.find_longest_match(prefix.data() + pos, prefix.size() - pos);
        query_tokens.push_back(match->first);
        pos += match->second;
    }

    size_t q_len = query_tokens.size();

    // 2. Precompute intervals
    std::vector<std::pair<uint32_t, uint32_t>> intervals;
    intervals.reserve(q_len + 1);
    
    size_t current_pos = 0;

    std::vector<uint8_t> scratch_buffer;
    scratch_buffer.resize(prefix.size());

    for (size_t i = 0; i < q_len; ++i) {
        const uint8_t* suffix_ptr = prefix.data() + current_pos;
        size_t suffix_len = prefix.size() - current_pos;

        auto [lb, ub] = valid_divergence(suffix_ptr, suffix_len, scratch_buffer.data());
        intervals.emplace_back(lb, ub);
        
        // Advance position by length of current token
        size_t t_start = token_boundaries[query_tokens[i]];
        size_t t_len = token_boundaries[query_tokens[i] + 1] - t_start;
        current_pos += t_len;
    }

    // Push a "Universal Interval" that accepts any token
    intervals.emplace_back(0, std::numeric_limits<uint32_t>::max());

    // 3. Scan
    size_t match_count = 0;
    const uint16_t* compressed_base = compressed_data.data();
    const size_t* boundaries_ptr = string_boundaries.data();
    const uint16_t* query_begin = query_tokens.data();

    for (size_t idx = 0; idx < string_boundaries.size() - 1; ++idx) {
        size_t start = boundaries_ptr[idx];
        size_t end = boundaries_ptr[idx + 1];
        size_t doc_len = end - start;
        size_t min_len = doc_len < q_len ? doc_len : q_len;
        const uint16_t* doc_begin = compressed_base + start;

        // Find mismatch index
        size_t diff_idx = 0;
        while (diff_idx < min_len && doc_begin[diff_idx] == query_begin[diff_idx]) {
            diff_idx++;
        }

        // Condition A: Exact Prefix Match (We processed the whole query length)
        if (diff_idx == q_len) {
            buffer[match_count++] = idx;
            continue;
        }

        // Condition B: Valid Divergence
        // We only check intervals if we are not at the end of the document
        // (If diff_idx == doc_len, the document ended before the query => mismatch)
        if (diff_idx < doc_len) {
            const uint16_t token = doc_begin[diff_idx];
            const auto& [lb, ub] = intervals[diff_idx];
            
            if (static_cast<uint16_t>(token - lb) < static_cast<uint16_t>(ub - lb)) {
                buffer[match_count++] = idx;
            }
        }
    }
    
    return match_count;
}

// This is a control variable implementation that does not assume a sorted dictionary.
// It allows us to evaluate the speedup enabled by a sorted dictionary (prefix_filtering_sorted).
size_t OnPairCompressor::prefix_filtering_unsorted(
    const LongestPrefixMatcher<uint16_t>& lpm, 
    const std::vector<uint8_t>& prefix, 
    size_t* buffer
) const { 
    // 1. Parse prefix into tokens
    std::vector<uint16_t> query_tokens;
    std::vector<size_t> query_offsets;
    query_tokens.reserve(prefix.size()); 
    query_offsets.reserve(prefix.size());

    size_t pos = 0;
    while (pos < prefix.size()) {
        auto match = lpm.find_longest_match(prefix.data() + pos, prefix.size() - pos);
        query_tokens.push_back(match->first);
        query_offsets.push_back(pos);
        pos += match->second;
    }

    size_t q_len = query_tokens.size();

    // 2. Scan
    size_t match_count = 0;
    const uint16_t* compressed_base = compressed_data.data();
    const size_t* boundaries_ptr = string_boundaries.data();
    const uint16_t* query_begin = query_tokens.data();
    const uint8_t* dict_base = dictionary.data();
    const uint32_t* token_offsets_ptr = token_boundaries.data();

    for (size_t idx = 0; idx < string_boundaries.size() - 1; ++idx) {
        size_t start = boundaries_ptr[idx];
        size_t end = boundaries_ptr[idx + 1];
        size_t doc_len = end - start;
        size_t min_len = doc_len < q_len ? doc_len : q_len;
        const uint16_t* doc_begin = compressed_base + start;

        // Find mismatch index
        size_t diff_idx = 0;
        while (diff_idx < min_len && doc_begin[diff_idx] == query_begin[diff_idx]) {
            diff_idx++;
        }

        // Condition A: Exact Prefix Match (We processed the whole query length)
        if (diff_idx == q_len) {
            buffer[match_count++] = idx;
            continue;
        }

        // Condition B: Valid Divergence
        // Explicitly access the dictionary to check the token
        if (diff_idx < doc_len) {
            const uint16_t token = doc_begin[diff_idx];

            // Mismatching token
            size_t t_start = token_offsets_ptr[token];
            size_t t_len = token_offsets_ptr[token + 1] - t_start;
            const uint8_t* token_ptr = dict_base + t_start;

            // Remaining suffix of the query
            size_t q_offset = query_offsets[diff_idx];
            size_t q_suffix_len = prefix.size() - q_offset;
            const uint8_t* query_suffix_ptr = prefix.data() + q_offset;
            
            // Comparison Logic
            // The mismatching token must start with the query suffix bytes.
            if (t_len >= q_suffix_len) {
                if (std::memcmp(token_ptr, query_suffix_ptr, q_suffix_len) == 0) {
                    buffer[match_count++] = idx;
                }
            }
        }
    }
    
    return match_count;
}

std::vector<OnPairCompressor::TokenStats> OnPairCompressor::get_token_statistics() const {
    size_t num_tokens = token_boundaries.size() - 1;
    std::vector<size_t> frequencies(num_tokens, 0);

    for (uint16_t token_id : compressed_data) {
        frequencies[token_id]++;
    }

    std::vector<TokenStats> stats;
    stats.reserve(num_tokens); 

    const uint32_t* offsets_ptr = token_boundaries.data();
    for (size_t id = 0; id < num_tokens; ++id) {
        size_t len = offsets_ptr[id + 1] - offsets_ptr[id];
        stats.push_back({static_cast<uint16_t>(id), frequencies[id], len});
    }

    return stats;
}