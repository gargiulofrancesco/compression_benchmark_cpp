#ifndef DEFLATE_INDIVIDUAL_COMPRESSOR_H
#define DEFLATE_INDIVIDUAL_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor

class DeflateIndividualCompressor : public Compressor<DeflateIndividualCompressor> {
private:

    std::vector<uint8_t> compressed_data;
    std::vector<size_t> compressed_end_positions;
    size_t data_size;

public:
    DeflateIndividualCompressor(size_t data_size, size_t n_elements);

    // Derived class method implementations
    void compress(const uint8_t* data, const std::vector<size_t>& compressed_end_positions);
    size_t decompress(uint8_t* buffer) const;
    size_t get_item_at(size_t index, uint8_t* buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;
};

#endif // DEFLATE_INDIVIDUAL_COMPRESSOR_H
