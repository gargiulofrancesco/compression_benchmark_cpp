#include "copy_compressor.h"
#include <cstring>

CopyCompressor::CopyCompressor(size_t data_size, size_t n_elements) {
    compressed_data.reserve(data_size);
    offsets.reserve(n_elements);
}

void CopyCompressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    size_t data_size = end_positions.size() == 0 ? 0 : end_positions.back();

    compressed_data.resize(data_size);
    offsets.resize(end_positions.size());

    // Copy raw data into the allocated memory
    std::memcpy(compressed_data.data(), data, data_size);
    std::memcpy(offsets.data(), end_positions.data(), end_positions.size() * sizeof(size_t));
}

// Assumes buffer has enough space to store the decompressed data
size_t CopyCompressor::decompress(uint8_t* buffer) const {
    size_t size = compressed_data.size();

    // Copy the internal compressed_data to the provided buffer
    std::memcpy(buffer, compressed_data.data(), size);

    return size;
}

// Assumes buffer has enough space to store the decompressed data
size_t CopyCompressor::get_item_at(size_t index, uint8_t* buffer) const {
    unsigned start = (index == 0) ? 0 : offsets[index - 1];
    unsigned end = offsets[index];
    size_t size = end - start;

    // Copy the data from the compressed_data
    std::memcpy(buffer, compressed_data.data() + start, size);

    return size;
}

size_t CopyCompressor::space_used_bytes() const {
    return compressed_data.size();
}

const char* CopyCompressor::name() const {
    return "Copy";
}
