#ifndef ONPAIR_COMPRESSOR_H
#define ONPAIR_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor
#include "lpm.h"

class OnPairCompressor : public Compressor<OnPairCompressor> {
private:
    static constexpr size_t FAST_COPY_SIZE = 16;

    std::vector<uint16_t> compressed_data;
    std::vector<size_t> string_boundaries;
    std::vector<uint8_t> dictionary; 
    std::vector<uint32_t> token_boundaries; 
    size_t threshold;
    size_t seed;

public:
    OnPairCompressor(size_t data_size, size_t n_elements);

    // Getters and Setters
    void set_threshold(size_t threshold);
    void set_seed(size_t seed);
    size_t get_threshold() const;
    size_t get_seed() const;

    // Derived class method implementations
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions);
    size_t decompress(uint8_t* buffer) const;
    size_t get_item_at(size_t index, uint8_t* buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;

    // Compression helper methods
    std::pair<LongestPrefixMatcher<uint16_t>, size_t> train_dictionary(const uint8_t* data, const std::vector<size_t>& end_positions, const std::vector<int>& shuffled_indices);
    void parse_data(const uint8_t* data, const std::vector<size_t>& end_positions, const LongestPrefixMatcher<uint16_t>& lpm);
    std::vector<int> generate_random_permutation(const size_t n_elements);
    
    // Prefix filtering methods
    LongestPrefixMatcher<uint16_t> sort_dictionary();
    uint32_t lower_bound(const std::vector<uint8_t>& target) const;
    size_t prefix_search(const LongestPrefixMatcher<uint16_t>& lpm, const std::vector<uint8_t>& prefix, size_t* buffer) const;
};

#endif // ONPAIR_COMPRESSOR_H
