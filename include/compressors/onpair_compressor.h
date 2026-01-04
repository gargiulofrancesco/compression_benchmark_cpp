#ifndef ONPAIR_COMPRESSOR_H
#define ONPAIR_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor
#include "lpm.h"

class OnPairCompressor : public Compressor<OnPairCompressor> {
private:
    static constexpr size_t THRESHOLD = 10;
    static constexpr size_t FAST_COPY_SIZE = 16;

    std::vector<uint16_t> compressed_data;
    std::vector<size_t> string_boundaries;
    std::vector<uint8_t> dictionary; 
    std::vector<uint32_t> token_boundaries; 

public:
    OnPairCompressor(size_t data_size, size_t n_elements);

    /**
     * Trains the dictionary using the provided data and permutation.
     * @return A pair containing the trained LongestPrefixMatcher and the total size in bytes of the data processed during training.
     */
    std::pair<LongestPrefixMatcher<uint16_t>, size_t> train_dictionary(const uint8_t* data, const std::vector<size_t>& end_positions, const size_t threshold, const std::vector<int>& shuffled_indices);
    void parse_data(const uint8_t* data, const std::vector<size_t>& end_positions, const LongestPrefixMatcher<uint16_t>& lpm);
    std::vector<int> generate_random_permutation(const size_t n_elements, const size_t seed = 42);

    // Derived class method implementations
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions);
    size_t decompress(uint8_t* buffer) const;
    size_t get_item_at(size_t index, uint8_t* buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;
};

#endif // ONPAIR_COMPRESSOR_H
