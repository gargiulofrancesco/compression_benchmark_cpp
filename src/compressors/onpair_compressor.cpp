#include "onpair_compressor.h"

OnPairCompressor::OnPairCompressor(size_t data_size, size_t n_elements) 
    : onpair(n_elements, data_size) {
}

void OnPairCompressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    onpair.compress_bytes(data, end_positions);
}

size_t OnPairCompressor::decompress(uint8_t* buffer) const {
    return onpair.decompress_all(buffer);
}

size_t OnPairCompressor::get_item_at(size_t index, uint8_t* buffer) const {
    return onpair.decompress_string(index, buffer);
}

size_t OnPairCompressor::space_used_bytes() const {
    return onpair.space_used();
}

const char* OnPairCompressor::name() const {
    return "OnPair";
}
