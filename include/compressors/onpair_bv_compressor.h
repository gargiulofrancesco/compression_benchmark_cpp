#ifndef ONPAIR_BV_COMPRESSOR_H
#define ONPAIR_BV_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor
#include "lpm.h"
#include "bitvector.h"

class OnPairBVCompressor : public Compressor<OnPairBVCompressor> {
private:
    static constexpr size_t FAST_ACCESS_SIZE = 16;
    static constexpr size_t BITS_PER_TOKEN = 13;
    static constexpr uint32_t MAX_TOKEN_ID = (1 << BITS_PER_TOKEN) - 1;

    BitVector compressed_data;
    std::vector<size_t> offsets;
    std::vector<uint8_t> dictionary_data;
    std::vector<uint32_t> dictionary_offsets;

    LongestPrefixMatcher<uint32_t> train(const uint8_t* data, const std::vector<size_t>& end_positions);
    void parse(const uint8_t* data, const std::vector<size_t>& end_positions, const LongestPrefixMatcher<uint32_t>& lpm);

public:
    OnPairBVCompressor(size_t data_size, size_t n_elements);

    // Derived class method implementations
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions);
    size_t decompress(uint8_t* buffer) const;
    size_t get_item_at(size_t index, uint8_t* buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;
};

#endif // ONPAIR_BV_COMPRESSOR_H
