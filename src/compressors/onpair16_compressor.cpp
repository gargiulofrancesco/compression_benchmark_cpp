#include "onpair16_compressor.h"

OnPair16Compressor::OnPair16Compressor(size_t data_size, size_t n_elements) 
    : onpair16(n_elements, data_size) {
}

void OnPair16Compressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    onpair16.compress_bytes(data, end_positions);
}

size_t OnPair16Compressor::decompress(uint8_t* buffer) const {
    return onpair16.decompress_all(buffer);
}

size_t OnPair16Compressor::get_item_at(size_t index, uint8_t* buffer) const {
    return onpair16.decompress_string(index, buffer);
}

size_t OnPair16Compressor::space_used_bytes() const {
    return onpair16.space_used();
}

const char* OnPair16Compressor::name() const {
    return "OnPair16";
}
