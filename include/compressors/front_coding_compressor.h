#ifndef FRONT_CODING_COMPRESSOR_H
#define FRONT_CODING_COMPRESSOR_H

#include "compressor.h" // Includes the base class Compressor
#include <cstdint>
#include <cstring>

/**
 * Front Coding (also known as Incremental Encoding) Compressor with Blocking
 * 
 * Efficient compression for sorted string dictionaries where consecutive
 * strings share common prefixes. Uses blocking for O(BLOCK_SIZE) random access:
 *   - Strings are grouped into blocks of BLOCK_SIZE strings
 *   - First string in each block is stored fully (prefix_len = 0)
 *   - Subsequent strings are front-coded relative to previous string in block
 * 
 * Each encoded string consists of:
 *   - prefix_length: length of shared prefix with previous string (varint)
 *   - suffix_length: length of the differing suffix (varint)
 *   - suffix_data: the actual suffix bytes
 * 
 * Random access requires decoding at most BLOCK_SIZE strings.
 */
class FrontCodingCompressor : public Compressor<FrontCodingCompressor> {
public:
    static constexpr size_t BLOCK_SIZE = 16;  // Strings per block (tune for space/speed tradeoff)

private:
    std::vector<uint8_t> encoded_data;     // Compressed data (prefix_len + suffix_len + suffix)
    std::vector<uint32_t> block_offsets;   // Byte offset to start of each block
    size_t n_elements;                     // Number of strings

    // Varint encoding/decoding helpers (inline for performance)
    static inline size_t encode_varint(uint8_t* buffer, uint32_t value) {
        size_t bytes = 0;
        while (value >= 0x80) {
            buffer[bytes++] = static_cast<uint8_t>(value | 0x80);
            value >>= 7;
        }
        buffer[bytes++] = static_cast<uint8_t>(value);
        return bytes;
    }

    static inline size_t decode_varint(const uint8_t* buffer, uint32_t& value) {
        value = 0;
        size_t shift = 0;
        size_t bytes = 0;
        uint8_t byte;
        do {
            byte = buffer[bytes++];
            value |= static_cast<uint32_t>(byte & 0x7F) << shift;
            shift += 7;
        } while (byte & 0x80);
        return bytes;
    }

    // Compute common prefix length between two byte sequences
    static inline size_t common_prefix_length(
        const uint8_t* a, size_t len_a,
        const uint8_t* b, size_t len_b
    ) {
        size_t min_len = (len_a < len_b) ? len_a : len_b;
        size_t i = 0;
        // Process 8 bytes at a time for better performance
        while (i + 8 <= min_len) {
            uint64_t va, vb;
            std::memcpy(&va, a + i, 8);
            std::memcpy(&vb, b + i, 8);
            if (va != vb) break;
            i += 8;
        }
        // Finish byte by byte
        while (i < min_len && a[i] == b[i]) {
            ++i;
        }
        return i;
    }

public:
    FrontCodingCompressor(size_t data_size, size_t n_elements);

    // Derived class method implementations
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions);
    size_t decompress(uint8_t* buffer) const;
    size_t get_item_at(size_t index, uint8_t* buffer) const;
    size_t space_used_bytes() const;
    const char* name() const;
    
    // Accessors for benchmarking
    size_t block_size() const { return BLOCK_SIZE; }
    size_t num_blocks() const { return (n_elements + BLOCK_SIZE - 1) / BLOCK_SIZE; }
};

#endif // FRONT_CODING_COMPRESSOR_H
