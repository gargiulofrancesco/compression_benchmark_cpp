#include "lz4_compressor.h"
#include <lz4.h>
#include <stdexcept>

LZ4Compressor::LZ4Compressor(size_t data_size, size_t n_elements)
    : BlockCompressor<LZ4Compressor>(data_size, n_elements) {}

size_t LZ4Compressor::compress_block(const uint8_t* block, size_t block_size) {
    auto& compressed_data = this->get_compressed_data();
    size_t start_pos = compressed_data.size();
    
    // Calculate max compressed size according to LZ4 docs
    size_t max_compressed_size = LZ4_compressBound(block_size);
    compressed_data.resize(start_pos + max_compressed_size);
    
    // Compress the block
    int compressed_size = LZ4_compress_default(
        reinterpret_cast<const char*>(block),
        reinterpret_cast<char*>(compressed_data.data() + start_pos),
        block_size,
        max_compressed_size
    );
    
    if (compressed_size <= 0) {
        throw std::runtime_error("LZ4 compression failed");
    }
    
    // Resize to actual compressed size
    compressed_data.resize(start_pos + compressed_size);
    return compressed_size;
}

void LZ4Compressor::decompress_block(
    const uint8_t* compressed_data,
    size_t compressed_size,
    uint8_t* buffer,
    size_t uncompressed_size
) const {
    int result = LZ4_decompress_safe(
        reinterpret_cast<const char*>(compressed_data),
        reinterpret_cast<char*>(buffer),
        compressed_size,
        uncompressed_size
    );
    
    if (result < 0) {
        throw std::runtime_error("LZ4 decompression failed");
    }
}

const char* LZ4Compressor::name() const {
    return "LZ4";
}