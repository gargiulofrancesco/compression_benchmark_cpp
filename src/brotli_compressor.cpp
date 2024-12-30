#include "brotli_compressor.h"
#include <brotli/encode.h>
#include <brotli/decode.h>

BrotliCompressor::BrotliCompressor(size_t data_size, size_t n_elements)
    : BlockCompressor<BrotliCompressor>(data_size, n_elements) {}

size_t BrotliCompressor::compress_block(const uint8_t* block, size_t block_size) {
    auto& compressed_data = this->get_compressed_data();
    size_t start_pos = compressed_data.size();
    size_t max_compressed_size = BrotliEncoderMaxCompressedSize(block_size);
    compressed_data.resize(start_pos + max_compressed_size);
    
    size_t compressed_size = max_compressed_size;
    if (!BrotliEncoderCompress(
            BROTLI_DEFAULT_QUALITY, 
            BROTLI_DEFAULT_WINDOW, 
            BROTLI_DEFAULT_MODE,
            block_size, 
            block,
            &compressed_size, 
            compressed_data.data() + start_pos)) {
        throw std::runtime_error("Brotli compression failed");
    }
    
    compressed_data.resize(start_pos + compressed_size);
    return compressed_size;
}

void BrotliCompressor::decompress_block(
    const uint8_t* compressed_data,
    size_t compressed_size,
    uint8_t* buffer,
    size_t uncompressed_size
) const {
    size_t decoded_size = uncompressed_size;
    if (!BrotliDecoderDecompress(
            compressed_size,
            compressed_data,
            &decoded_size,
            buffer)) {
        throw std::runtime_error("Brotli decompression failed");
    }
}

const char* BrotliCompressor::name() const {
    return "Brotli";
}