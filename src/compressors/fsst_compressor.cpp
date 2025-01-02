#include "fsst_compressor.h"
#include <cstring>
#include <limits>

FSSTCompressor::FSSTCompressor(size_t data_size, size_t n_elements) {
    compressed_data.resize(data_size);
    offsets.resize(n_elements);

    // Preallocate vectors for compression
    row_lengths.resize(n_elements);
    row_ptrs.resize(n_elements);
    compressed_row_lengths.resize(n_elements);
    compressed_row_ptrs.resize(n_elements + 1);
}

void FSSTCompressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    size_t num_strings = end_positions.size();
    
    // Prepare row lengths and pointers based on end positions
    for (size_t i = 0; i < num_strings; ++i) {
        size_t start = (i == 0) ? 0 : end_positions[i - 1];
        size_t end = end_positions[i];
        row_lengths[i] = end - start;
        row_ptrs[i] = data + start;
    }

    // Initialize encoder
    auto encoder = fsst_create(num_strings, row_lengths.data(), row_ptrs.data(), false);

    // Perform compression
    fsst_compress(encoder, num_strings, row_lengths.data(), row_ptrs.data(),
                    compressed_data.size(), compressed_data.data(),
                    compressed_row_lengths.data(), compressed_row_ptrs.data());

    // Calculate the compressed data size
    size_t compressed_length = compressed_row_ptrs[num_strings - 1] + compressed_row_lengths[num_strings - 1] - compressed_data.data();
    compressed_data.resize(compressed_length);

    // Calculate offsets for random access
    compressed_row_ptrs[num_strings] = compressed_data.data() + compressed_length;
    for (unsigned i = 0; i < num_strings; ++i) {
        offsets[i] = compressed_row_ptrs[i + 1] - compressed_data.data();
    }

    // Export the dictionary and destroy the encoder
    uint8_t buffer[sizeof(fsst_decoder_t)];
    fsst_export(encoder, buffer);
    fsst_destroy(encoder);

    // Import the dictionary into the decoder
    fsst_import(&decoder, buffer);
}

// Assumes buffer has enough space to store the decompressed data
size_t FSSTCompressor::decompress(uint8_t* buffer) const {
    size_t max_decompressed_size = std::numeric_limits<size_t>::max();

    size_t decompressed_length = fsst_decompress(
        &decoder,
        offsets.back(),
        compressed_data.data(),
        max_decompressed_size,
        buffer
    );
 
    return decompressed_length;
}

// Assumes buffer has enough space to store the decompressed data
size_t FSSTCompressor::get_item_at(size_t index, uint8_t* buffer) const {
    size_t start = (index == 0) ? 0 : offsets[index - 1];
    size_t end = offsets[index];
    size_t max_decompressed_size = offsets.back();

    size_t decompressed_length = fsst_decompress(
        &decoder,
        end - start,
        compressed_data.data() + start,
        max_decompressed_size,
        buffer
    );

    return decompressed_length;
}

size_t FSSTCompressor::space_used_bytes() const {
    return compressed_data.size() + sizeof(decoder.len) + sizeof(decoder.symbol);
}

const char* FSSTCompressor::name() const {
    return "FSST";
}
