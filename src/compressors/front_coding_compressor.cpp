#include "front_coding_compressor.h"
#include <cstring>
#include <algorithm>

FrontCodingCompressor::FrontCodingCompressor(size_t data_size, size_t n_elements) 
    : n_elements(0) {
    encoded_data.reserve(data_size);
    block_offsets.reserve(n_elements / BLOCK_SIZE + 1);
}

void FrontCodingCompressor::compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
    n_elements = end_positions.size() - 1;
    if (n_elements == 0) return;
    
    // Temporary buffer for varint encoding (max 10 bytes for two varints)
    uint8_t varint_buf[10];
    
    const uint8_t* prev_str = nullptr;
    size_t prev_len = 0;

    for (size_t i = 0; i < n_elements; ++i) {
        size_t start = end_positions[i];
        size_t end = end_positions[i + 1];
        size_t str_len = end - start;
        const uint8_t* str = data + start;

        // At block boundaries, reset prefix dependency and record block offset
        bool is_block_start = (i % BLOCK_SIZE == 0);
        if (is_block_start) {
            block_offsets.push_back(static_cast<uint32_t>(encoded_data.size()));
        }
        
        size_t prefix_len = 0;
        if (!is_block_start && prev_str != nullptr) {
            prefix_len = common_prefix_length(prev_str, prev_len, str, str_len);
        }
        size_t suffix_len = str_len - prefix_len;

        // Encode prefix_len and suffix_len as varints
        size_t varint_bytes = 0;
        varint_bytes += encode_varint(varint_buf + varint_bytes, static_cast<uint32_t>(prefix_len));
        varint_bytes += encode_varint(varint_buf + varint_bytes, static_cast<uint32_t>(suffix_len));

        // Append varints to encoded data
        encoded_data.insert(encoded_data.end(), varint_buf, varint_buf + varint_bytes);

        // Append suffix bytes
        encoded_data.insert(encoded_data.end(), str + prefix_len, str + str_len);

        prev_str = str;
        prev_len = str_len;
    }

    // Final offset (marks end of last block)
    block_offsets.push_back(static_cast<uint32_t>(encoded_data.size()));

    // Shrink to fit to reclaim unused reserved memory
    encoded_data.shrink_to_fit();
    block_offsets.shrink_to_fit();
}

// Assumes buffer has enough space to store the decompressed data
size_t FrontCodingCompressor::decompress(uint8_t* buffer) const {
    if (n_elements == 0) return 0;

    uint8_t* out = buffer;
    const uint8_t* prev_str = nullptr;
    size_t prev_len = 0;
    const uint8_t* ptr = encoded_data.data();

    for (size_t i = 0; i < n_elements; ++i) {
        // At block boundaries, reset prefix reference
        if (i % BLOCK_SIZE == 0) {
            prev_str = nullptr;
            prev_len = 0;
        }

        uint32_t prefix_len, suffix_len;
        ptr += decode_varint(ptr, prefix_len);
        ptr += decode_varint(ptr, suffix_len);

        // Copy prefix from previous string
        if (prefix_len > 0 && prev_str != nullptr) {
            std::memcpy(out, prev_str, prefix_len);
        }

        // Copy suffix
        std::memcpy(out + prefix_len, ptr, suffix_len);
        ptr += suffix_len;

        prev_str = out;
        prev_len = prefix_len + suffix_len;
        out += prev_len;
    }

    return static_cast<size_t>(out - buffer);
}

// Assumes buffer has enough space to store the decompressed data
size_t FrontCodingCompressor::get_item_at(size_t index, uint8_t* buffer) const {
    if (index >= n_elements) return 0;

    // Determine block and position within block
    size_t block_idx = index / BLOCK_SIZE;
    size_t block_start_idx = block_idx * BLOCK_SIZE;
    
    // Start decoding from block offset
    const uint8_t* ptr = encoded_data.data() + block_offsets[block_idx];
    size_t current_len = 0;
    
    for (size_t i = block_start_idx; i <= index; ++i) {
        uint32_t prefix_len, suffix_len;
        ptr += decode_varint(ptr, prefix_len);
        ptr += decode_varint(ptr, suffix_len);

        // Copy suffix after the prefix (prefix is already in buffer from previous iteration)
        std::memmove(buffer + prefix_len, ptr, suffix_len);
        ptr += suffix_len;
        current_len = prefix_len + suffix_len;
    }

    return current_len;
}

size_t FrontCodingCompressor::space_used_bytes() const {
    return encoded_data.size() + block_offsets.size() * sizeof(uint32_t);
}

const char* FrontCodingCompressor::name() const {
    return "Front Coding";
}
