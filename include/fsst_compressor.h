#ifndef FSST_COMPRESSOR_H
#define FSST_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor
#include "fsst.h"  // FSST decoder header

class FSSTCompressor : public Compressor<FSSTCompressor> {
private:
    std::vector<uint8_t> compressed_data;
    std::vector<size_t> offsets;
    fsst_decoder_t decoder;

public:
    FSSTCompressor(size_t data_size, size_t n_elements);

    // Derived class method implementations
    void compress(const std::vector<uint8_t>& data, const std::vector<size_t>& end_positions);
    void decompress(std::vector<uint8_t>& buffer);
    void get_item_at(size_t index, std::vector<uint8_t>& buffer);
    size_t space_used_bytes() const;
    const char* name() const;
};

#endif // FSST_COMPRESSOR_H
