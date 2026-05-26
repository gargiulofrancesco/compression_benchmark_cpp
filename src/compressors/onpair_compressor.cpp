#include "onpair_compressor.h"

OnPairCompressor::OnPairCompressor(size_t data_size, size_t n_elements) {
    config_.bits = 16;
    config_.threshold = onpair::encoding::DynamicThreshold{0.15};
}

void OnPairCompressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    // The library expects an array of uint32_t offsets.
    std::vector<uint32_t> offsets(end_positions.size());
    for (size_t i = 0; i < end_positions.size(); ++i) {
        offsets[i] = static_cast<uint32_t>(end_positions[i]);
    }
    
    // end_positions has size N + 1 where N is the number of strings
    const size_t n = end_positions.size() - 1;
    
    column_ = onpair::OnPairColumn::compress(
        reinterpret_cast<const char*>(data), 
        offsets.data(), 
        n, 
        config_
    );
}

size_t OnPairCompressor::decompress(uint8_t* buffer) const {
    return column_.view().decompress_all(reinterpret_cast<char*>(buffer));
}

size_t OnPairCompressor::get_item_at(size_t index, uint8_t* buffer) const {
    return column_.view().decompress(index, reinterpret_cast<char*>(buffer));
}

size_t OnPairCompressor::space_used_bytes() const {
    return column_.bytes_used();
}

const char* OnPairCompressor::name() const {
    return "OnPair";
}