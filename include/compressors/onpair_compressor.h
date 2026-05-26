#ifndef ONPAIR_COMPRESSOR_H
#define ONPAIR_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor
#include <onpair/api.h>

class OnPairCompressor : public Compressor<OnPairCompressor> {
private:
    onpair::OnPairColumn column_;
    onpair::encoding::TrainingConfig config_;

public:
    OnPairCompressor(size_t data_size, size_t n_elements);

    // Allowing dynamic configuration before compress()
    void set_config(const onpair::encoding::TrainingConfig& config) {
        config_ = config;
    }

    // Derived class method implementations
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions);
    size_t decompress(uint8_t* buffer) const;
    size_t get_item_at(size_t index, uint8_t* buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;
};

#endif // ONPAIR_COMPRESSOR_H