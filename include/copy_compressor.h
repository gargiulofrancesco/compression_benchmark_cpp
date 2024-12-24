#ifndef COPY_COMPRESSOR_H
#define COPY_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor

class CopyCompressor : public Compressor<CopyCompressor> {
private:
    alignas(64) std::vector<uint8_t> compressed_data;
    alignas(64) std::vector<size_t> offsets;

public:
    CopyCompressor(size_t data_size, size_t n_elements);

    // Derived class method implementations
    void compress(const std::vector<uint8_t>& data, const std::vector<size_t>& end_positions);
    void decompress(std::vector<uint8_t>& buffer) const;
    void get_item_at(size_t index, std::vector<uint8_t>& buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;
};

#endif // COPY_COMPRESSOR_H
