#ifndef ONPAIR16_COMPRESSOR_H
#define ONPAIR16_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor
#include "lpm16.h"

class OnPair16Compressor : public Compressor<OnPair16Compressor> {
private:
    static constexpr size_t THRESHOLD = 10;
    static constexpr size_t MAX_LENGTH = 16;
    static constexpr size_t MAX_TOKENS = 65535;

    std::vector<uint16_t> compressed_data;
    std::vector<size_t> offsets;
    std::vector<uint8_t> dictionary_data;
    std::vector<uint32_t> dictionary_offsets;

    LongestPrefixMatcher16 train(const uint8_t* data, const std::vector<size_t>& end_positions);
    void parse(const uint8_t* data, const std::vector<size_t>& end_positions, const LongestPrefixMatcher16& lpm);

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
