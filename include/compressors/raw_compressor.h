#ifndef RAW_COMPRESSOR_H
#define RAW_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor

class RawCompressor : public Compressor<RawCompressor> {
private:
    std::vector<uint8_t> compressed_data;
    std::vector<size_t> offsets;

public:
    RawCompressor(size_t data_size, size_t n_elements);

    // Derived class method implementations
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions);
    size_t decompress(uint8_t* buffer) const;
    size_t get_item_at(size_t index, uint8_t* buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;
};

#endif // RAW_COMPRESSOR_H
