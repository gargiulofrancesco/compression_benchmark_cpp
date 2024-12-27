#include "lz4_compressor.h"

LZ4Compressor::LZ4Compressor(size_t data_size, size_t n_elements)
    : BlockCompressor<LZ4Compressor>(data_size, n_elements) {}

size_t LZ4Compressor::compress_block(const uint8_t* block, size_t block_size) {
    auto& compressed_data = this->get_compressed_data();
    size_t start_pos = compressed_data.size();
    compressed_data.resize(start_pos + block_size);

    // Use memcpy for copying raw memory
    std::memcpy(compressed_data.data() + start_pos, block, block_size);

    return block_size;
}

void LZ4Compressor::decompress_block(
    const uint8_t* compressed_data,
    size_t compressed_size,
    uint8_t* buffer,
    size_t uncompressed_size
) const {
    std::memcpy(buffer, compressed_data, compressed_size);
}

const char* LZ4Compressor::name() const {
    return "LZ4";
}