#ifndef SNAPPY_COMPRESSOR_H
#define SNAPPY_COMPRESSOR_H

#include "block_compressor.h"
#include <cstddef>
#include <cstdint>


class SnappyCompressor : public BlockCompressor<SnappyCompressor> {
public:
    explicit SnappyCompressor(size_t data_size, size_t n_elements);
    size_t compress_block(const uint8_t* block, size_t block_size);
    void decompress_block(const uint8_t* compressed_data, size_t compressed_size, uint8_t* buffer, size_t uncompressed_size) const;
    const char* name() const;
};

#endif // SNAPPY_COMPRESSOR_H
