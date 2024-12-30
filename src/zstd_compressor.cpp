#include "zstd_compressor.h"
#include <zstd.h>
#include <stdexcept>

ZstdCompressor::ZstdCompressor(size_t data_size, size_t n_elements)
    : BlockCompressor<ZstdCompressor>(data_size, n_elements) {}

size_t ZstdCompressor::compress_block(const uint8_t* block, size_t block_size) {
    auto& compressed_data = this->get_compressed_data();
    size_t start_pos = compressed_data.size();
    size_t max_compressed_size = ZSTD_compressBound(block_size);
    compressed_data.resize(start_pos + max_compressed_size);
    
    size_t compressed_size = ZSTD_compress(
        compressed_data.data() + start_pos,
        max_compressed_size,
        block,
        block_size,
        ZSTD_defaultCLevel()
    );
    
    if (ZSTD_isError(compressed_size)) {
        throw std::runtime_error(ZSTD_getErrorName(compressed_size));
    }
    
    compressed_data.resize(start_pos + compressed_size);
    return compressed_size;
}

void ZstdCompressor::decompress_block(
    const uint8_t* compressed_data,
    size_t compressed_size,
    uint8_t* buffer,
    size_t uncompressed_size
) const {
    size_t result = ZSTD_decompress(
        buffer,
        uncompressed_size,
        compressed_data,
        compressed_size
    );
    
    if (ZSTD_isError(result)) {
        throw std::runtime_error(ZSTD_getErrorName(result));
    }
}

const char* ZstdCompressor::name() const {
    return "Zstd";
}