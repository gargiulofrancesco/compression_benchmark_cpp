#include "deflate_compressor.h"
#include <zlib.h>
#include <stdexcept>

DeflateCompressor::DeflateCompressor(size_t data_size, size_t n_elements)
    : BlockCompressor<DeflateCompressor>(data_size, n_elements) {}

size_t DeflateCompressor::compress_block(const uint8_t* block, size_t block_size) {
    auto& compressed_data = this->get_compressed_data();
    size_t start_pos = compressed_data.size();
    uLongf max_compressed_size = compressBound(block_size);
    compressed_data.resize(start_pos + max_compressed_size);
    
    uLongf compressed_size = max_compressed_size;
    int result = compress2(
        compressed_data.data() + start_pos,
        &compressed_size,
        block,
        block_size,
        Z_BEST_COMPRESSION
    );
    
    if (result != Z_OK) {
        throw std::runtime_error("Deflate compression failed");
    }
    
    compressed_data.resize(start_pos + compressed_size);
    return compressed_size;
}

void DeflateCompressor::decompress_block(
    const uint8_t* compressed_data,
    size_t compressed_size,
    uint8_t* buffer,
    size_t uncompressed_size
) const {
    uLongf dest_len = uncompressed_size;
    int result = uncompress(
        buffer,
        &dest_len,
        compressed_data,
        compressed_size
    );
    
    if (result != Z_OK) {
        throw std::runtime_error("Deflate decompression failed");
    }
}

const char* DeflateCompressor::name() const {
    return "Deflate";
}