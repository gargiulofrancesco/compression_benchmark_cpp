#ifndef ONPAIR16_COMPRESSOR_H
#define ONPAIR16_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor
#include "onpair16.h"

class OnPair16Compressor : public Compressor<OnPair16Compressor> {
private:
    OnPair16 onpair16;

public:
    OnPair16Compressor(size_t data_size, size_t n_elements);

    // Derived class method implementations
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions);
    size_t decompress(uint8_t* buffer) const;
    size_t get_item_at(size_t index, uint8_t* buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;
};

#endif // ONPAIR16_COMPRESSOR_H
