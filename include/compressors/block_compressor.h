#ifndef BLOCK_COMPRESSOR_H
#define BLOCK_COMPRESSOR_H

#include "compressor.h"
#include <cstring>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cassert>

/**
 * Metadata for compressed blocks in block-based compression schemes
 *
 * Stores essential information for block boundary management and random access
 * within compressed datasets divided into fixed-size blocks.
 */
struct BlockMetadata {
    size_t end_position;        // End position of this block in compressed data
    size_t num_items_psum;      // Cumulative number of items up to this block
    int32_t uncompressed_size;  // Uncompressed size of this block
};

/**
 * Abstract base class for block-based compression algorithms
 *
 * Provides infrastructure for compressors that divide input data into fixed-size blocks
 * for independent compression. Enables efficient random access by maintaining block
 * metadata and implementing block-level caching for repeated accesses.
 */
template <typename Derived>
class BlockCompressor : public Compressor<Derived> {
private:
    static constexpr size_t DEFAULT_BLOCK_SIZE = 64 * 1024;  // 64 KB
    size_t block_size_;
    std::vector<BlockMetadata> blocks_metadata_;
    std::vector<uint8_t> compressed_data_;
    std::vector<size_t> item_end_positions_;
    mutable std::vector<uint8_t> block_cache_;
    mutable int32_t block_cache_index_ = -1;

public:
    /**
     * Constructor for block-based compressors
     *
     * Initializes internal data structures and reserves memory based on estimated
     * requirements. Sets default block size to 64KB for balanced compression
     * ratio and random access performance.
     *
     * @param data_size Total size of input data in bytes
     * @param n_elements Number of individual strings in the dataset
     */
    BlockCompressor(size_t data_size, size_t n_elements)
        : block_size_(DEFAULT_BLOCK_SIZE) {
        compressed_data_.reserve(data_size);
        blocks_metadata_.reserve(data_size / block_size_ + 1);
        item_end_positions_.reserve(n_elements);
        block_cache_.resize(block_size_);
    }

    /**
     * Compresses a single block using the derived algorithm
     *
     * Pure virtual method that must be implemented by derived classes.
     * Compresses the given block and appends result to internal storage.
     *
     * @param block Pointer to uncompressed block data
     * @param block_size Size of the uncompressed block in bytes
     * @return Size of compressed block in bytes
     */
    inline size_t compress_block(const uint8_t* block, size_t block_size) {
        return static_cast<Derived*>(this)->compress_block(block, block_size);
    }

    /**
     * Decompresses a single block using the derived algorithm
     *
     * Pure virtual method that must be implemented by derived classes.
     * Decompresses compressed block data to the provided buffer.
     *
     * @param compressed_data Pointer to compressed block data
     * @param compressed_size Size of compressed data in bytes
     * @param buffer Output buffer for decompressed data
     * @param uncompressed_size Size of decompressed data
     */
    inline void decompress_block(const uint8_t* compressed_data, size_t compressed_size, uint8_t* buffer, size_t uncompressed_size) const {
        static_cast<const Derived*>(this)->decompress_block(compressed_data, compressed_size, buffer, uncompressed_size);
    }

    /**
     * Compresses the entire dataset using block-based approach
     *
     * Divides input data into blocks of approximately block_size_ bytes, ensuring
     * string boundaries are preserved. Each block is compressed independently
     * and metadata is stored for efficient random access.
     *
     * @param data Raw byte array containing concatenated strings
     * @param end_positions Boundary positions for individual strings (cumulative lengths)
     */
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
        item_end_positions_ = end_positions;
        size_t block_start = 0;         // Start of the current block
        size_t num_items_in_block = 0;  // Number of items in the current block
        size_t current_block_size = 0;  // Total size of the current block
        size_t item_start = 0;          // Start of the current item

        for(int i=1; i<end_positions.size(); i++) {
            size_t item_end = end_positions[i];
            size_t item_size = item_end - item_start;
            
            if (current_block_size + item_size > block_size_) {
                size_t block_size = item_start - block_start;
                size_t compressed_block_size = compress_block(data + block_start, block_size);
                size_t end_position = compressed_block_size + (blocks_metadata_.empty() ? 0 : blocks_metadata_.back().end_position);
                size_t num_items_psum = num_items_in_block + (blocks_metadata_.empty() ? 0 : blocks_metadata_.back().num_items_psum);

                blocks_metadata_.push_back({
                    end_position,
                    num_items_psum,
                    static_cast<int32_t>(block_size)
                });

                block_start = item_start;
                num_items_in_block = 0;
                current_block_size = 0;
            }

            current_block_size += item_size;
            num_items_in_block++;
            item_start = item_end;
        }

        if (num_items_in_block > 0) {
            size_t block_size = item_start - block_start;
            size_t compressed_block_size = compress_block(data + block_start, block_size);
            size_t end_position = blocks_metadata_.empty() ? compressed_block_size : blocks_metadata_.back().end_position + compressed_block_size;
            size_t num_items_psum = num_items_in_block + (blocks_metadata_.empty() ? 0 : blocks_metadata_.back().num_items_psum);

            blocks_metadata_.push_back({
                end_position,
                num_items_psum,
                static_cast<int32_t>(block_size)
            });
        }
    }

    /**
     * Decompresses all blocks to reconstruct the original dataset
     *
     * Sequentially decompresses each block and concatenates results into
     * the output buffer.
     *
     * @param buffer Pre-allocated output buffer for decompressed data
     * @return Total number of bytes written to the buffer
     */
    size_t decompress(uint8_t* buffer) const {
        size_t total_size = 0;
        for (size_t i = 0; i < blocks_metadata_.size(); ++i) {
            const auto& block_metadata = blocks_metadata_[i];
            size_t start = i == 0 ? 0 : blocks_metadata_[i - 1].end_position;
            size_t compressed_size = block_metadata.end_position - start;
            
            decompress_block(
                compressed_data_.data() + start,
                compressed_size,
                buffer + total_size,
                block_metadata.uncompressed_size
            );
            
            total_size += block_metadata.uncompressed_size;
        }
        return total_size;
    }

    /**
     * Retrieves a single string by index with optimized random access
     *
     * Locates the block containing the requested string, decompresses only
     * that block (with caching), and extracts the specific string.
     * Implements block-level caching to amortize decompression costs.
     *
     * @param index Zero-based index of the string to retrieve
     * @param buffer Output buffer for the decompressed string
     * @return Number of bytes written to the buffer
     */
    inline size_t get_item_at(size_t index, uint8_t* buffer) const {
        size_t block_index = get_block_index(index);
        decompress_block_to_cache(block_index);

        auto [item_start, item_end] = get_item_delimiters(block_index, index);
        size_t item_size = item_end - item_start;
        std::memcpy(buffer, block_cache_.data() + item_start, item_size);

        return item_size;
    }

    /**
     * Reports total memory usage of the compressed representation
     *
     * Includes compressed data and block metadata overhead.
     * Does not include temporary buffers or caches.
     *
     * @return Total bytes used by compressed data and metadata structures
     */
    size_t space_used_bytes() const {
        return compressed_data_.size() + blocks_metadata_.size() * sizeof(BlockMetadata);
    }

    /**
     * Provides access to compressed data for derived classes
     *
     * @return Reference to internal compressed data vector
     */
    inline std::vector<uint8_t>& get_compressed_data() { return compressed_data_; }

private:
    /**
     * Finds the block index containing the specified string
     *
     * Uses binary search on cumulative item counts to efficiently locate
     * the target block for random access operations.
     *
     * @param item_index Zero-based index of the target string
     * @return Index of the block containing the string
     */
    size_t get_block_index(size_t item_index) const {
        return std::partition_point(
            blocks_metadata_.begin(),
            blocks_metadata_.end(),
            [item_index](const BlockMetadata& block) {
                return item_index >= block.num_items_psum;
            }
        ) - blocks_metadata_.begin();
    }

    /**
     * Calculates start and end positions of a string within its block
     *
     * Translates global string positions to block-relative coordinates
     * for efficient extraction from decompressed block data.
     *
     * @param block_index Index of the block containing the string
     * @param item_index Global index of the target string
     * @return Pair of (start_offset, end_offset) within the block
     */
    std::pair<size_t, size_t> get_item_delimiters(size_t block_index, size_t item_index) const {
        assert(block_index < blocks_metadata_.size());

        size_t first_item_index = block_index == 0 ? 0 : 
            blocks_metadata_[block_index - 1].num_items_psum;

        size_t start = item_end_positions_[item_index];
        size_t end = item_end_positions_[item_index+1];
        size_t adjustment = first_item_index > 0 ? item_end_positions_[first_item_index] : 0;
        

        return {start - adjustment, end - adjustment};
    }

    /**
     * Decompresses a block to the internal cache for repeated access
     *
     * Implements block-level caching to avoid repeated decompression of the
     * same block during sequential or clustered random access patterns.
     * Updates cache state and validates block index bounds.
     *
     * @param block_index Index of the block to decompress and cache
     */
    void decompress_block_to_cache(size_t block_index) const {
        if (block_cache_index_ == block_index) {
            return;
        }

        const auto& block_metadata = blocks_metadata_[block_index];
        size_t start = block_index == 0 ? 0 : blocks_metadata_[block_index - 1].end_position;
        size_t compressed_size = block_metadata.end_position - start;

        block_cache_.resize(block_metadata.uncompressed_size);
        decompress_block(
            compressed_data_.data() + start,
            compressed_size,
            block_cache_.data(),
            block_metadata.uncompressed_size
        );

        block_cache_index_ = static_cast<int32_t>(block_index);
    }
};

#endif // BLOCK_COMPRESSOR_H