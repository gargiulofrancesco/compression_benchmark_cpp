#include <queue>
#include <robin_hood.h>
#include "bitvector.h"
#include "bpe_compressor.h"
#include "pair_hash.h"

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

    // The bitvector inidicates with zeroes the positions of merged bytes
    BitVector bit_vector = BitVector::with_ones(end_positions.back());

    // Strings end positions are used to avoid merging pairs that cross multiple strings
    robin_hood::unordered_set<size_t> end_positions_set(end_positions.begin() + 1, end_positions.end());

    // Initialize pair positions    
    robin_hood::unordered_map<std::pair<uint16_t, uint16_t>, robin_hood::unordered_set<size_t>, pair_hash> pair_positions;
    for (size_t i=0; i<end_positions.back()-1; i++) {
        if(end_positions_set.contains(i+1)) {
            continue;
        }

        uint16_t t1 = token_ids[i];
        uint16_t t2 = token_ids[i+1];
        pair_positions[{t1, t2}].insert(i);
    }

    // Initialize heap tracking the most frequent pairs
    std::priority_queue<std::pair<size_t, std::pair<uint16_t, uint16_t>>> heap;
    for (const auto& pair : pair_positions) {
        size_t frequency = pair.second.size();
        heap.push({frequency, pair.first});
    }

    // Merge pairs
    uint32_t next_token_id = 256;
    while (next_token_id<MAX_TOKENS && !heap.empty()) {
        // Get the pair with the maximum (uodated) frequency
        if(heap.top().first != pair_positions[heap.top().second].size()) {
            heap.pop();
            continue;
        }

        auto [t1, t2] = heap.top().second;
        heap.pop();

        // Get the positions of the pair
        std::vector<size_t> positions(pair_positions[{t1, t2}].begin(), pair_positions[{t1, t2}].end());
        std::sort(positions.begin(), positions.end());
        pair_positions.erase({t1, t2});

        // Add the new token to the dictionary
        auto t1_start = dictionary_offsets[t1];
        auto t1_end = dictionary_offsets[t1 + 1];
        auto t2_start = dictionary_offsets[t2];
        auto t2_end = dictionary_offsets[t2 + 1];
        dictionary_data.insert(dictionary_data.end(), dictionary_data.data() + t1_start, dictionary_data.data() + t1_end);
        dictionary_data.insert(dictionary_data.end(), dictionary_data.data() + t2_start, dictionary_data.data() + t2_end);
        dictionary_offsets.push_back(dictionary_data.size());

        // Store updated pairs to minimize insertions in the heap
        robin_hood::unordered_set<std::pair<uint16_t, uint16_t>, pair_hash> updated_pairs_set;

        // Update occurrences of (t1, t2)
        for(size_t pos : positions) {
            // If position was already merged, skip
            if (!bit_vector.get(pos).value()) {
                continue;
            }

            auto t1_pos = pos;
            auto t2_pos = bit_vector.next_one(t1_pos).value();
            auto t0_pos = bit_vector.prev_one(t1_pos);
            auto t3_pos = bit_vector.next_one(t2_pos);

            // Update (t0, t1) and (t0, next_id)     
            if (t0_pos.has_value() && !end_positions_set.contains(t1_pos)) {
                auto t0 = token_ids[t0_pos.value()];
                // Update (t0, t1)
                if (t0!=t1 || t1!=t2) {
                    pair_positions[{t0, t1}].erase(t0_pos.value());
                    updated_pairs_set.insert({t0, t1});
                }
                // Update (t0, next_id)
                pair_positions[{t0, next_token_id}].insert(t0_pos.value());
                updated_pairs_set.insert({t0, next_token_id});
            }

            // Update (t2, t3) and (next_id, t3)
            if (t3_pos.has_value() && !end_positions_set.contains(t3_pos.value())) {
                auto t3 = token_ids[t3_pos.value()];
                // Update (t2, t3)
                if(t2!=t1 || t3!=t2) {
                    pair_positions[{t2, t3}].erase(t2_pos);
                    updated_pairs_set.insert({t2, t3});
                }
                // Update (next_id, t3)
                pair_positions[{next_token_id, t3}].insert(t1_pos);
                updated_pairs_set.insert({next_token_id, t3});
            }

            // set t2_pos to 0 to merge t1 and t2
            bit_vector.set(t2_pos, false);

            // Update token_ids
            token_ids[t1_pos] = next_token_id;
        }

        // Update the heap
        for (const auto& pair : updated_pairs_set) {
            auto frequency = pair_positions[pair].size();
            heap.push({frequency, pair});
        }

        next_token_id++;
    }

    // Store the compressed data
    size_t i = 0;
    for (auto end_pos : end_positions) {
        while (i < end_pos) {
            if (bit_vector.get(i).value()) {
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

        std::memcpy(buffer + size, dict_ptr + dict_start, FAST_ACCESS_SIZE);
        if(length > FAST_ACCESS_SIZE) {
            std::memcpy(buffer + size + FAST_ACCESS_SIZE, dict_ptr + dict_start + FAST_ACCESS_SIZE, length - FAST_ACCESS_SIZE);
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
        std::memcpy(buffer + size, dict_ptr + dict_start, FAST_ACCESS_SIZE);
        if(length > FAST_ACCESS_SIZE) {
            std::memcpy(buffer + size + FAST_ACCESS_SIZE, dict_ptr + dict_start + FAST_ACCESS_SIZE, length - FAST_ACCESS_SIZE);
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
