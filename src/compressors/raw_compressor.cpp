#include "raw_compressor.h"
#include <cstring>

RawCompressor::RawCompressor(size_t data_size, size_t n_elements) {
    uncompressed_data.resize(data_size);
    offsets.resize(n_elements + 1);
}

void RawCompressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    std::memcpy(offsets.data(), end_positions.data(), end_positions.size() * sizeof(size_t));
    std::memcpy(uncompressed_data.data(), data, end_positions.back());
}

// Assumes buffer has enough space to store the decompressed data
size_t RawCompressor::decompress(uint8_t* buffer) const {
    size_t size = uncompressed_data.size();

    // Copy the internal uncompressed_data to the provided buffer
    std::memcpy(buffer, uncompressed_data.data(), size);

    return size;
}

// Assumes buffer has enough space to store the decompressed data
size_t RawCompressor::get_item_at(size_t index, uint8_t* buffer) const {
    unsigned start = offsets[index];
    unsigned end = offsets[index + 1];
    size_t size = end - start;

    // Copy the data from the uncompressed_data
    std::memcpy(buffer, uncompressed_data.data() + start, size);

    return size;
}

size_t RawCompressor::prefix_filtering(const std::vector<uint8_t>& prefix, const std::vector<size_t>& candidates, size_t* buffer) const {    
    size_t count = 0;
    size_t p_len = prefix.size();
    const uint8_t* p_data = prefix.data();

    const size_t* offsets_ptr = offsets.data();
    const uint8_t* data_base = uncompressed_data.data();

    for (auto idx : candidates) {
        size_t start = offsets_ptr[idx];
        size_t len = offsets_ptr[idx + 1] - start;

        // Length Filter
        if (len < p_len) continue;

        // Memcmp Comparison
        if (std::memcmp(data_base + start, p_data, p_len) == 0) {
            buffer[count++] = idx;
        }
    }

    return count;
}

size_t RawCompressor::space_used_bytes() const {
    return uncompressed_data.size();
}

const char* RawCompressor::name() const {
    return "Raw";
}
