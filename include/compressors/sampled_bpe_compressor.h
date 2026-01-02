#ifndef SAMPLED_BPE_COMPRESSOR_H
#define SAMPLED_BPE_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor
#include "lpm.h" // From external/onpair

class SampledBPECompressor : public Compressor<SampledBPECompressor> {
private:
    static constexpr size_t FAST_ACCESS_SIZE = 16;
    static constexpr float SAMPLE_SIZE_PERCENTAGE = 0.1; 

    std::vector<uint16_t> compressed_data;
    std::vector<size_t> offsets;
    std::vector<uint8_t> dictionary_data;
    std::vector<uint32_t> dictionary_offsets;

    LongestPrefixMatcher<uint16_t> train(const uint8_t* data, const std::vector<size_t>& end_positions);
    void parse(const uint8_t* data, const std::vector<size_t>& end_positions, const LongestPrefixMatcher<uint16_t>& lpm);
    std::pair<std::vector<uint8_t>, std::vector<size_t>> sampling(const uint8_t* data, const std::vector<size_t>& end_positions, const size_t sample_size);

public:
SampledBPECompressor(size_t data_size, size_t n_elements);

    // Derived class method implementations
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions);
    size_t decompress(uint8_t* buffer) const;
    size_t get_item_at(size_t index, uint8_t* buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;
};

#endif // SAMPLED_BPE_COMPRESSOR_H