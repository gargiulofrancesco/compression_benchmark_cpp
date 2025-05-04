#ifndef ONPAIRBPE_COMPRESSOR_H16_COMPRESSOR_H
#define BPE_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor
#include <cstring>
#include <unordered_map>
#include "bitvector.h"
#include <robin_hood.h>
#include "pair_hash.h"

class BPECompressor : public Compressor<BPECompressor> {
private:
    static constexpr size_t MAX_TOKENS = 65535;
    static constexpr size_t FAST_ACCESS_SIZE = 16;

    std::vector<uint16_t> compressed_data;
    std::vector<size_t> offsets;
    std::vector<uint8_t> dictionary_data;
    std::vector<uint32_t> dictionary_offsets;

    void merge(const uint8_t* data, const std::vector<size_t>& end_positions);

public:
BPECompressor(size_t data_size, size_t n_elements);

    // Derived class method implementations
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions);
    size_t decompress(uint8_t* buffer) const;
    size_t get_item_at(size_t index, uint8_t* buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;
};

#endif // BPE_COMPRESSOR_H
