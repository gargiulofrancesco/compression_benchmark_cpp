#ifndef SAMPLED_BPE_COMPRESSOR_H
#define SAMPLED_BPE_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor
#include "lpm.h" 

class SampledBPECompressor : public Compressor<SampledBPECompressor> {
private:
    static constexpr size_t FAST_COPY_SIZE = 16;

    std::vector<uint16_t> compressed_data;
    std::vector<size_t> string_boundaries;
    std::vector<uint8_t> dictionary;
    std::vector<uint32_t> token_boundaries;
    size_t sample_size;
    size_t seed;

public:
    SampledBPECompressor(size_t data_size, size_t n_elements);

    // Getters and Setters
    void set_sample_size(size_t sample_size);
    void set_seed(size_t seed);
    size_t get_sample_size() const;
    size_t get_seed() const;
    
    // Derived class method implementations
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions);
    size_t decompress(uint8_t* buffer) const;
    size_t get_item_at(size_t index, uint8_t* buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;

    // Compression helper methods
    LongestPrefixMatcher<uint16_t> train_dictionary(const uint8_t* data, const std::vector<size_t>& end_positions);
    void parse_data(const uint8_t* data, const std::vector<size_t>& end_positions, const LongestPrefixMatcher<uint16_t>& lpm);
    std::pair<std::vector<uint8_t>, std::vector<size_t>> sampling(const uint8_t* data, const std::vector<size_t>& end_positions);
};

#endif // SAMPLED_BPE_COMPRESSOR_H