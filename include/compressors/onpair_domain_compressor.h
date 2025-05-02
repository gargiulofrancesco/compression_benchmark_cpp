#ifndef ONPAIR_DOMAIN_COMPRESSOR_H
#define ONPAIR_DOMAIN_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor
#include "lpm.h"

class OnPairDomainCompressor : public Compressor<OnPairDomainCompressor> {
private:
    static constexpr size_t THRESHOLD = 10;
    static constexpr size_t FAST_ACCESS_SIZE = 16;
    static constexpr size_t K = 5000;
    static constexpr size_t MAX_TOKENS = 65535 - K;
    static constexpr size_t SAMPLE_SIZE = 32 * 1024 * 1024;

    std::vector<uint16_t> compressed_data;
    std::vector<size_t> offsets;
    std::vector<uint8_t> dictionary_data;
    std::vector<uint32_t> dictionary_offsets;

    LongestPrefixMatcher train(const uint8_t* data, const std::vector<size_t>& end_positions);
    void parse(const uint8_t* data, const std::vector<size_t>& end_positions, const LongestPrefixMatcher& lpm, const std::unordered_map<size_t, uint16_t>& top_k_parsing);
    std::tuple<std::vector<uint8_t>, std::vector<size_t>, std::unordered_map<size_t, uint16_t>> find_top_k(const uint8_t* data, const std::vector<size_t>& end_positions, const size_t k);
    std::pair<std::vector<uint8_t>, std::vector<size_t>> sampling(const uint8_t* data, const std::vector<size_t>& end_positions, const size_t sample_size, const std::unordered_map<size_t, uint16_t>& top_k_parsing);

public:
    OnPairDomainCompressor(size_t data_size, size_t n_elements);

    // Derived class method implementations
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions);
    size_t decompress(uint8_t* buffer) const;
    size_t get_item_at(size_t index, uint8_t* buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;
};

#endif // ONPAIR_DOMAIN_COMPRESSOR_H
