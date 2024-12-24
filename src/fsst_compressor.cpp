#include "fsst_compressor.h"
#include <cstring>

FSSTCompressor::FSSTCompressor(size_t data_size, size_t n_elements)
    : compressed_data(data_size), offsets(n_elements) {
    // Memory is allocated and initialized upfront to avoid runtime overhead.
}

void FSSTCompressor::compress(const std::vector<uint8_t>& data, const std::vector<size_t>& end_positions) {
    // Preallocate vectors for compression
    size_t num_strings = end_positions.size();
    std::vector<size_t> row_lengths(num_strings);
    std::vector<const uint8_t*> row_ptrs(num_strings);
    std::vector<size_t> compressed_row_lengths(num_strings);
    std::vector<uint8_t*> compressed_row_ptrs(num_strings + 1);

    // Prepare row lengths and pointers based on end positions
    for (size_t i = 0; i < num_strings; ++i) {
        size_t start = (i == 0) ? 0 : end_positions[i - 1];
        size_t end = end_positions[i];
        row_lengths[i] = end - start;
        row_ptrs[i] = reinterpret_cast<const uint8_t*>(&data[start]);
    }

    // Initialize encoder
    auto encoder = fsst_create(num_strings, row_lengths.data(), row_ptrs.data(), false);

    // Preallocate the compression buffer with a size large enough for the worst case
    size_t total_length = data.size();
    std::vector<uint8_t> compression_buffer(16 + 2 * total_length);

    // Perform compression
    fsst_compress(encoder, num_strings, row_lengths.data(), row_ptrs.data(),
                    compression_buffer.size(), compression_buffer.data(),
                    compressed_row_lengths.data(), compressed_row_ptrs.data());

    // Calculate the compressed data size
    size_t compressed_length = compressed_row_ptrs[num_strings - 1] +
                                        compressed_row_lengths[num_strings - 1] -
                                        compression_buffer.data();
    compressed_data.resize(compressed_length);
    memcpy(compressed_data.data(), compression_buffer.data(), compressed_length);

    // Calculate offsets for random access
    offsets.resize(num_strings);
    compressed_row_ptrs[num_strings] = compression_buffer.data() + compressed_length;
    for (unsigned i = 0; i < num_strings; ++i) {
        offsets[i] = compressed_row_ptrs[i + 1] - compression_buffer.data();
    }

    // Export the dictionary and destroy the encoder
    uint8_t buffer[sizeof(fsst_decoder_t)];
    fsst_export(encoder, buffer);
    fsst_destroy(encoder);

    // Import the dictionary into the decoder
    fsst_import(&decoder, buffer);
}

// Assumes buffer has enough space to store the decompressed data
void FSSTCompressor::decompress(std::vector<uint8_t>& buffer) const {
    size_t max_decompressed_size = buffer.capacity(); 

    unsigned decompressed_length = fsst_decompress(
        &decoder,
        offsets.back(),
        compressed_data.data(),
        max_decompressed_size,
        buffer.data()
    );
 
    // Adjust the buffer size to the actual decompressed length
    buffer.resize(decompressed_length);
}

// Assumes buffer has enough space to store the decompressed data
void FSSTCompressor::get_item_at(size_t index, std::vector<uint8_t>& buffer) const {
    unsigned start = (index == 0) ? 0 : offsets[index - 1];
    unsigned end = offsets[index];
    size_t max_decompressed_size = buffer.capacity(); 

    unsigned decompressed_length = fsst_decompress(
        &decoder,
        end - start,
        compressed_data.data() + start,
        max_decompressed_size,
        buffer.data()
    );

    // Adjust the buffer size to the actual decompressed length
    buffer.resize(decompressed_length);
}

size_t FSSTCompressor::space_used_bytes() const {
    return compressed_data.size() + sizeof(decoder.len) + sizeof(decoder.symbol);
}

const char* FSSTCompressor::name() const {
    return "FSST";
}
