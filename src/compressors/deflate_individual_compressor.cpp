#include "deflate_individual_compressor.h"
#include <zlib.h>
#include <stdexcept>

DeflateIndividualCompressor::DeflateIndividualCompressor(size_t data_size, size_t n_elements) {
    compressed_data.reserve(data_size);
    compressed_end_positions.reserve(n_elements);
    this->data_size = data_size;
}

void DeflateIndividualCompressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    compressed_end_positions.push_back(0);
    
    for(int i=0; i<end_positions.size()-1; i++) {
        size_t string_start = end_positions[i];
        size_t string_end = end_positions[i+1];

        if (string_start == string_end) {
            compressed_end_positions.push_back(compressed_data.size());
            continue;
        }

        size_t length = string_end - string_start;
        size_t offset = compressed_data.size();
        uLongf max_compressed_size = compressBound(length);               
        compressed_data.resize(offset + max_compressed_size);
        uLongf compressed_size = max_compressed_size;

        int result = compress2(
            compressed_data.data() + offset, 
            &compressed_size, 
            data + string_start, 
            length, 
            Z_BEST_COMPRESSION
        );

        if (result != Z_OK) {
            throw std::runtime_error("Compression failed");
        }

        compressed_data.resize(offset + compressed_size);
        compressed_end_positions.push_back(compressed_data.size());
    }
}

// Assumes buffer has enough space to store the decompressed data
size_t DeflateIndividualCompressor::decompress(uint8_t* buffer) const {
    size_t size = 0;

    for (size_t i = 0; i < compressed_end_positions.size() - 1; i++) {
        size_t start = compressed_end_positions[i];
        size_t end = compressed_end_positions[i + 1];

        if(start == end) {
            continue;
        }

        size_t length = end - start;
        uLongf dest_len = data_size - size;

        int result = uncompress(
            buffer + size, 
            &dest_len, 
            compressed_data.data() + start, 
            length
        );
        
        if (result != Z_OK) {
            throw std::runtime_error("Decompression failed");
        }

        size += dest_len;
    }

    return size;
}

// Assumes buffer has enough space to store the decompressed data
size_t DeflateIndividualCompressor::get_item_at(size_t index, uint8_t* buffer) const {
    size_t size = 0;

    size_t start = compressed_end_positions[index];
    size_t end = compressed_end_positions[index + 1];

    if (start == end) {
        return 0;
    }

    size_t length = end - start;
    uLongf dest_len = data_size;

    int result = uncompress(buffer, &dest_len, compressed_data.data() + start, length);
    if (result != Z_OK) {
        throw std::runtime_error("Decompression failed");
    }

    return dest_len;
}

size_t DeflateIndividualCompressor::space_used_bytes() const {
    return compressed_data.size();
}

const char* DeflateIndividualCompressor::name() const {
    return "DeflateIndividual";
}
