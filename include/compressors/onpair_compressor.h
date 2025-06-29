#ifndef ONPAIR_COMPRESSOR_H
#define ONPAIR_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor
#include "lpm.h"

class OnPairCompressor : public Compressor<OnPairCompressor> {
private:
    static constexpr size_t FAST_ACCESS_SIZE = 16;

    std::vector<uint16_t> compressed_data;
    std::vector<size_t> offsets;
    std::vector<uint8_t> dictionary_data;
    std::vector<uint32_t> dictionary_offsets;

    LongestPrefixMatcher<uint16_t> train(const uint8_t* data, const std::vector<size_t>& end_positions);
    void parse(const uint8_t* data, const std::vector<size_t>& end_positions, const LongestPrefixMatcher<uint16_t>& lpm);

public:
    OnPairCompressor(size_t data_size, size_t n_elements);

    // Derived class method implementations
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions);
    size_t decompress(uint8_t* buffer) const;
    size_t get_item_at(size_t index, uint8_t* buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;
};

#endif // ONPAIR_COMPRESSOR_H
