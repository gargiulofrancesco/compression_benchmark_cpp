#ifndef DEFLATE_COMPRESSOR_H
#define DEFLATE_COMPRESSOR_H

#include "block_compressor.h"
#include <cstddef>
#include <cstdint>


class DeflateCompressor : public BlockCompressor<DeflateCompressor> {
public:
    explicit DeflateCompressor(size_t data_size, size_t n_elements);
    size_t compress_block(const uint8_t* block, size_t block_size);
    void decompress_block(const uint8_t* compressed_data, size_t compressed_size, uint8_t* buffer, size_t uncompressed_size) const;
    const char* name() const;
};

#endif // DEFLATE_COMPRESSOR_H
