#include "fsst_block_compressor.h"
#include <cstring>
#include <limits>
#include <algorithm>

FSSTBlockCompressor::FSSTBlockCompressor(size_t data_size, size_t n_elements) {
    compressed_data.resize(data_size);
    offsets.reserve(n_elements);
}

void FSSTBlockCompressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    const size_t MAX_BLOCK_SIZE = 4 * 1024 * 1024; // 4 MiB blocks
    offsets.push_back(0);
    size_t num_strings = end_positions.size() - 1;

    size_t current_string = 0;
    size_t compressed_size = 0;

    while(current_string < num_strings) {
        size_t batch_bytes = 0;
        size_t batch_strings = 0;

        std::vector<size_t> row_lengths;
        std::vector<const uint8_t*> row_ptrs;
        
        // Gather strings into this batch
        while(current_string < num_strings) {
            size_t start = end_positions[current_string];
            size_t end = end_positions[current_string + 1];
            size_t string_length = end - start;

            if (batch_bytes + string_length > MAX_BLOCK_SIZE && batch_strings > 0) {
                break;
            }

            row_lengths.push_back(string_length);
            row_ptrs.push_back(data + start);

            batch_bytes += string_length;
            ++batch_strings;
            ++current_string;
        }

        std::vector<size_t> compressed_row_lengths(batch_strings);
        std::vector<uint8_t*> compressed_row_ptrs(batch_strings);

        // Initialize encoder
        fsst_encoder_t* encoder = fsst_create(batch_strings, row_lengths.data(), row_ptrs.data(), false);

        // Perform compression
        fsst_compress(encoder, batch_strings, row_lengths.data(), row_ptrs.data(),
                        compressed_data.size() - compressed_size, compressed_data.data() + compressed_size,
                        compressed_row_lengths.data(), compressed_row_ptrs.data());

        compressed_size = compressed_row_ptrs[batch_strings - 1] + compressed_row_lengths[batch_strings - 1] - compressed_data.data();

        // Calculate offsets for random access
        for (unsigned i = 0; i < batch_strings; ++i) {
            offsets.push_back(offsets.back() + compressed_row_lengths[i]);
        }

        // Store the number of strings processed so far
        batch_sizes.push_back(current_string);

        // Export the dictionary and destroy the encoder
        uint8_t buffer[sizeof(fsst_decoder_t)];
        fsst_export(encoder, buffer);
        fsst_destroy(encoder);

        // Import the dictionary into the decoder
        fsst_decoder_t decoder;
        fsst_import(&decoder, buffer);
        decoders.push_back(decoder);
    }

    compressed_data.resize(compressed_size);
}

// Assumes buffer has enough space to store the decompressed data
size_t FSSTBlockCompressor::decompress(uint8_t* buffer) const {
    size_t max_decompressed_size = std::numeric_limits<size_t>::max();
    size_t decompressed_size = 0;
    size_t batch_start = 0;

    for(int batch = 0; batch < decoders.size(); ++batch) {
        size_t next_batch_start = batch_sizes[batch];
        size_t batch_size = offsets[next_batch_start] - offsets[batch_start];
        size_t batch_decompressed_size = fsst_decompress(
            &decoders[batch],
            batch_size,
            compressed_data.data() + offsets[batch_start],
            max_decompressed_size,
            buffer + decompressed_size
        );

        decompressed_size += batch_decompressed_size;
        batch_start = next_batch_start;
    }

    return decompressed_size;
}

// Assumes buffer has enough space to store the decompressed data
size_t FSSTBlockCompressor::get_item_at(size_t index, uint8_t* buffer) const {
    size_t start = offsets[index];
    size_t end = offsets[index + 1];
    size_t max_decompressed_size = offsets.back();

    // Determine which decoder to use
    auto it = std::upper_bound(batch_sizes.begin(), batch_sizes.end(), index);
    size_t decoder_id = std::distance(batch_sizes.begin(), it);

    size_t decompressed_size = fsst_decompress(
        &decoders[decoder_id],
        end - start,
        compressed_data.data() + start,
        max_decompressed_size,
        buffer
    );

    return decompressed_size;
}

size_t FSSTBlockCompressor::space_used_bytes() const {
    size_t decoders_size = 0;
    for (const auto& decoder : decoders) {
        decoders_size += sizeof(decoder.len) + sizeof(decoder.symbol);
    }

    return compressed_data.size() + decoders_size;
}

const char* FSSTBlockCompressor::name() const {
    return "FSST Block";
}
