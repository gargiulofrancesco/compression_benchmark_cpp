#include "copy_compressor.h"
#include <cstring>

CopyCompressor::CopyCompressor(size_t data_size, size_t n_elements) {
    compressed_data.resize(data_size);
    offsets.resize(n_elements + 1);
}

void CopyCompressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    offsets[0] = 0;
    if (!end_positions.empty()) {
        std::memcpy(offsets.data() + 1, end_positions.data(), end_positions.size() * sizeof(size_t));
    }

    std::memcpy(compressed_data.data(), data, end_positions.back());
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
    unsigned start = offsets[index];
    unsigned end = offsets[index + 1];
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
