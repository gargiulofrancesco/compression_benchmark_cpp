#include "xz_compressor.h"
#include <lzma.h>

XZCompressor::XZCompressor(size_t data_size, size_t n_elements)
    : BlockCompressor<XZCompressor>(data_size, n_elements) {}

size_t XZCompressor::compress_block(const uint8_t* block, size_t block_size) {
    auto& compressed_data = this->get_compressed_data();
    size_t start_pos = compressed_data.size();
    size_t max_compressed_size = block_size + (block_size / 3) + 128;
    compressed_data.resize(start_pos + max_compressed_size);
    
    lzma_stream strm = LZMA_STREAM_INIT;
    if (lzma_easy_encoder(&strm, LZMA_PRESET_DEFAULT, LZMA_CHECK_CRC64) != LZMA_OK) {
        throw std::runtime_error("XZ compression initialization failed");
    }
    
    strm.next_in = block;
    strm.avail_in = block_size;
    strm.next_out = compressed_data.data() + start_pos;
    strm.avail_out = max_compressed_size;
    
    lzma_ret ret = lzma_code(&strm, LZMA_FINISH);
    lzma_end(&strm);
    
    if (ret != LZMA_STREAM_END) {
        throw std::runtime_error("XZ compression failed");
    }
    
    size_t compressed_size = max_compressed_size - strm.avail_out;
    compressed_data.resize(start_pos + compressed_size);
    return compressed_size;
}

void XZCompressor::decompress_block(
    const uint8_t* compressed_data,
    size_t compressed_size,
    uint8_t* buffer,
    size_t uncompressed_size
) const {
    lzma_stream strm = LZMA_STREAM_INIT;
    if (lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED) != LZMA_OK) {
        throw std::runtime_error("XZ decompression initialization failed");
    }
    
    strm.next_in = compressed_data;
    strm.avail_in = compressed_size;
    strm.next_out = buffer;
    strm.avail_out = uncompressed_size;
    
    lzma_ret ret = lzma_code(&strm, LZMA_FINISH);
    lzma_end(&strm);
    
    if (ret != LZMA_STREAM_END) {
        throw std::runtime_error("XZ decompression failed");
    }
}

const char* XZCompressor::name() const {
    return "XZ";
}