#include <queue>
#include <robin_hood.h>
#include "bitvector.h"
#include "bpe_compressor.h"
#include "pair_hash.h"

using Pair = std::pair<uint16_t, uint16_t>;

BPECompressor::BPECompressor(size_t data_size, size_t n_elements) {
    compressed_data.reserve(data_size);
    offsets.reserve(n_elements);
    dictionary_data.reserve(2 * (1024 * 1024)); // 2 MB
    dictionary_offsets.reserve(1 << 16);
}

void BPECompressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) { 
    // Initialize the dictionary with single-byte tokens
    dictionary_offsets.push_back(0);
    for(uint16_t i=0; i<=255; i++) {
        uint8_t value = static_cast<uint8_t>(i);
        dictionary_data.push_back(value);
        dictionary_offsets.push_back(dictionary_data.size());
    }

    // Initialize Token IDs
    std::vector<uint16_t> token_ids(end_positions.back(), 0);
    for (size_t i=0; i<end_positions.back(); i++) {
        token_ids[i] = data[i];
    }

    // A bitvector indicates with zeroes the positions of merged bytes.
    BitVector bv = BitVector::with_ones(end_positions.back());

    // Strings end positions are used to avoid merging pairs across different strings
    robin_hood::unordered_set<size_t> end_positions_set(end_positions.begin() + 1, end_positions.end());

    // Initialize pair positions    
    robin_hood::unordered_map<Pair, robin_hood::unordered_set<size_t>, PairHash> pair_pos;
    for (size_t i=0; i<end_positions.back()-1; i++) {
        if(end_positions_set.contains(i+1)) {
            continue;
        }

        uint16_t t1 = token_ids[i];
        uint16_t t2 = token_ids[i+1];
        pair_pos[{t1, t2}].insert(i);
    }

    // Initialize heap tracking the most frequent pairs
    std::priority_queue<std::pair<size_t, Pair>> top_pairs;
    for (const auto& pair : pair_pos) {
        size_t freq = pair.second.size();
        top_pairs.push({freq, pair.first});
    }

    // Merge pairs
    uint32_t next_token_id = 256;
    while (!top_pairs.empty()) {
        // Get the most frequent pair
        auto top = top_pairs.top();
        auto freq = top.first;
        auto [t1, t2] = top.second;
        top_pairs.pop();

        // Check if the frequency is up-to-date
        auto current_freq = pair_pos[{t1, t2}].size();
        if (freq != current_freq) {
            top_pairs.push({current_freq, {t1, t2}});
            continue;
        }

        // Stop if the most frequent pair has frequency 0
        if (current_freq == 0) {
            break;
        }

        // Get the positions of the pair
        std::vector<size_t> positions(pair_pos[{t1, t2}].begin(), pair_pos[{t1, t2}].end());
        std::sort(positions.begin(), positions.end());
        pair_pos.erase({t1, t2});

        // Add the new token to the dictionary
        auto t1_start = dictionary_offsets[t1];
        auto t1_end = dictionary_offsets[t1 + 1];
        auto t2_start = dictionary_offsets[t2];
        auto t2_end = dictionary_offsets[t2 + 1];
        dictionary_data.insert(dictionary_data.end(), dictionary_data.data() + t1_start, dictionary_data.data() + t1_end);
        dictionary_data.insert(dictionary_data.end(), dictionary_data.data() + t2_start, dictionary_data.data() + t2_end);
        dictionary_offsets.push_back(dictionary_data.size());

        // Keep track of new pairs that will form after merging
        robin_hood::unordered_set<Pair, PairHash> new_pairs;

        // Update occurrences of the top pair
        for(size_t pos : positions) {
            // If position was already merged, skip
            if (!bv.get(pos).value()) {
                continue;
            }

            // We indicate with t0 and t3 the tokens before and after the top pair
            auto t1_pos = pos;
            auto t2_pos = bv.next_one(t1_pos).value();
            auto t0_pos = bv.prev_one(t1_pos); // t0_pos is None if t1 is the first token
            auto t3_pos = bv.next_one(t2_pos); // t3_pos is None if t2 is the last token

            // Update (t0, t1) and (t0, next_id)     
            if (t0_pos.has_value() && !end_positions_set.contains(t1_pos)) {
                auto t0 = token_ids[t0_pos.value()];
                // Update (t0, t1)
                if (t0!=t1 || t1!=t2) {
                    pair_pos[{t0, t1}].erase(t0_pos.value());
                }
                // Update (t0, next_id)
                pair_pos[{t0, next_token_id}].insert(t0_pos.value());
                new_pairs.insert({t0, next_token_id});
            }

            // Update (t2, t3) and (next_id, t3)
            if (t3_pos.has_value() && !end_positions_set.contains(t3_pos.value())) {
                auto t3 = token_ids[t3_pos.value()];
                // Update (t2, t3)
                if(t2!=t1 || t3!=t2) {
                    pair_pos[{t2, t3}].erase(t2_pos);
                }
                // Update (next_id, t3)
                pair_pos[{next_token_id, t3}].insert(t1_pos);
                new_pairs.insert({next_token_id, t3});
            }

            // set t2_pos to 0 to merge t1 and t2
            bv.set(t2_pos, false);

            // Update token_ids
            token_ids[t1_pos] = next_token_id;
        }

        // Update the top_pairs heap with new pairs.
        // We don't need to update old pairs because they are already in the heap and their frequency can only decrease; 
        // the check at the beginning of the merge loop ensures we operate with up-to-date frequencies.
        for (const auto& pair : new_pairs) {
            auto freq = pair_pos[pair].size();
            top_pairs.push({freq, pair});
        }

        // If the dictionary is full, stop merging
        if (next_token_id == std::numeric_limits<uint16_t>::max()) {
            break;
        }
        
        next_token_id++;
    }

    // Store the compressed data
    size_t i = 0;
    for (auto end_pos : end_positions) {
        while (i < end_pos) {
            if (bv.get(i).value()) {
                compressed_data.push_back(token_ids[i]);
            }
            i++;
        }
        offsets.push_back(compressed_data.size());
    }
}

// Assumes buffer has enough space to store the decompressed data
size_t BPECompressor::decompress(uint8_t* buffer) const {
    const uint8_t* dict_ptr = dictionary_data.data();
    const uint32_t* offsets_ptr = dictionary_offsets.data();
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
size_t BPECompressor::get_item_at(size_t index, uint8_t* buffer) const {
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
        std::memcpy(buffer + size, dict_ptr + dict_start, FAST_COPY_SIZE);
        if(length > FAST_COPY_SIZE) {
            std::memcpy(buffer + size + FAST_COPY_SIZE, dict_ptr + dict_start + FAST_COPY_SIZE, length - FAST_COPY_SIZE);
        }

        size += length;
    }

    return size;
}

size_t BPECompressor::space_used_bytes() const {
    return compressed_data.size() * sizeof(uint16_t) + dictionary_data.size() + dictionary_offsets.size() * sizeof(uint32_t);
}

const char* BPECompressor::name() const {
    return "BPE";
}
