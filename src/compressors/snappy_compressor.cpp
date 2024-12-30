#include "snappy_compressor.h"
#include <snappy.h>
#include <stdexcept>

SnappyCompressor::SnappyCompressor(size_t data_size, size_t n_elements)
    : BlockCompressor<SnappyCompressor>(data_size, n_elements) {}

size_t SnappyCompressor::compress_block(const uint8_t* block, size_t block_size) {
    auto& compressed_data = this->get_compressed_data();
    size_t start_pos = compressed_data.size();
    
    size_t max_compressed_size = snappy::MaxCompressedLength(block_size);
    compressed_data.resize(start_pos + max_compressed_size);
    
    size_t compressed_size;
    snappy::RawCompress(
        reinterpret_cast<const char*>(block),
        block_size,
        reinterpret_cast<char*>(compressed_data.data() + start_pos),
        &compressed_size
    );
    
    compressed_data.resize(start_pos + compressed_size);
    return compressed_size;
}

void SnappyCompressor::decompress_block(
    const uint8_t* compressed_data,
    size_t compressed_size,
    uint8_t* buffer,
    size_t uncompressed_size
) const {
    if (!snappy::RawUncompress(
        reinterpret_cast<const char*>(compressed_data),
        compressed_size,
        reinterpret_cast<char*>(buffer))) {
        throw std::runtime_error("Snappy decompression failed");
    }
}

const char* SnappyCompressor::name() const {
    return "Snappy";
}