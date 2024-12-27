#ifndef BLOCK_COMPRESSOR_H
#define BLOCK_COMPRESSOR_H

#include "compressor.h"
#include <cstring>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cassert>

struct BlockMetadata {
    size_t end_position;        // End position of this block in compressed data
    size_t num_items_psum;      // Cumulative number of items up to this block
    int32_t uncompressed_size;  // Uncompressed size of this block
};

template <typename Derived>
class BlockCompressor : public Compressor<Derived> {
private:
    static constexpr size_t DEFAULT_BLOCK_SIZE = 64 * 1024;  // 64 KB
    size_t block_size_;
    std::vector<BlockMetadata> blocks_metadata_;
    std::vector<uint8_t> compressed_data_;
    std::vector<size_t> item_end_positions_;
    mutable std::vector<uint8_t> block_cache_;

public:
    BlockCompressor(size_t data_size, size_t n_elements)
        : block_size_(DEFAULT_BLOCK_SIZE) {
        compressed_data_.reserve(data_size);
        blocks_metadata_.reserve(data_size / block_size_ + 1);
        item_end_positions_.reserve(n_elements);
        block_cache_.reserve(block_size_);
    }

    size_t compress_block(const uint8_t* block, size_t block_size) {
        return static_cast<Derived*>(this)->compress_block(block, block_size);
    }

    void decompress_block(const uint8_t* compressed_data, size_t compressed_size, uint8_t* buffer, size_t uncompressed_size) const {
        static_cast<const Derived*>(this)->decompress_block(compressed_data, compressed_size, buffer, uncompressed_size);
    }

    void compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
        item_end_positions_ = end_positions;
        size_t block_start = 0;         // Start of the current block
        size_t num_items_in_block = 0;  // Number of items in the current block
        size_t current_block_size = 0;  // Total size of the current block
        size_t item_start = 0;          // Start of the current item

        for (size_t item_end : end_positions) {
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

    size_t get_item_at(size_t index, uint8_t* buffer) const {
        size_t block_index = get_block_index(index);
        decompress_block_to_cache(block_index);

        auto [item_start, item_end] = get_item_delimiters(block_index, index);
        size_t item_size = item_end - item_start;

        // Use memcpy with raw pointers
        std::memcpy(buffer, block_cache_.data() + item_start, item_size);

        return item_size;
    }

    size_t space_used_bytes() const {
        return compressed_data_.size() + blocks_metadata_.size() * sizeof(BlockMetadata);
    }

    inline std::vector<uint8_t>& get_compressed_data() { return compressed_data_; }

private:
    size_t get_block_index(size_t item_index) const {
        return std::partition_point(
            blocks_metadata_.begin(),
            blocks_metadata_.end(),
            [item_index](const BlockMetadata& block) {
                return item_index >= block.num_items_psum;
            }
        ) - blocks_metadata_.begin();
    }

    std::pair<size_t, size_t> get_item_delimiters(size_t block_index, size_t item_index) const {
        assert(block_index < blocks_metadata_.size());

        size_t first_item_index = block_index == 0 ? 0 : 
            blocks_metadata_[block_index - 1].num_items_psum;

        size_t start = item_index > 0 ? item_end_positions_[item_index - 1] : 0;
        size_t adjustment = first_item_index > 0 ? item_end_positions_[first_item_index - 1] : 0;
        size_t end = item_end_positions_[item_index];

        return {start - adjustment, end - adjustment};
    }

    void decompress_block_to_cache(size_t block_index) const {
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
    }
};

#endif // BLOCK_COMPRESSOR_H