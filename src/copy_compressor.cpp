#include "copy_compressor.h"
#include <cstring>
#include <iostream>

CopyCompressor::CopyCompressor(size_t data_size, size_t n_elements) {
    compressed_data.reserve(data_size);
    offsets.reserve(n_elements);
}

void CopyCompressor::compress(const std::vector<uint8_t>& data, const std::vector<size_t>& end_positions) {
    // Adjust the vector sizes
    compressed_data.resize(data.size());
    offsets.resize(end_positions.size());

    // Copy raw data into the allocated memory
    std::memcpy(compressed_data.data(), data.data(), data.size());
    std::memcpy(offsets.data(), end_positions.data(), end_positions.size() * sizeof(size_t));
}

// Assumes buffer has enough space to store the decompressed data
void CopyCompressor::decompress(std::vector<uint8_t>& buffer) const {
    // Adjust the buffer size to the actual decompressed length
    buffer.resize(compressed_data.size());

    // Copy the internal compressed_data to the provided buffer
    std::memcpy(buffer.data(), compressed_data.data(), compressed_data.size());
}

// Assumes buffer has enough space to store the decompressed data
void CopyCompressor::get_item_at(size_t index, std::vector<uint8_t>& buffer) const {
    unsigned start = (index == 0) ? 0 : offsets[index - 1];
    unsigned end = offsets[index];

    // Calculate the size of the item
    size_t size = end - start;

    // Adjust the buffer size to the actual decompressed length
    buffer.resize(size);

    // Copy the data from the compressed_data
    std::memcpy(buffer.data(), compressed_data.data() + start, size);
}

size_t CopyCompressor::space_used_bytes() const {
    return compressed_data.size();
}

const char* CopyCompressor::name() const {
    return "Copy";
}
