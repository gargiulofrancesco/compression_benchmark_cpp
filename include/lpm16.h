#ifndef LONGEST_PREFIX_MATCHER16_H
#define LONGEST_PREFIX_MATCHER16_H

#include <cstdint>
#include <vector>
#include <utility>
#include <optional>
#include <algorithm>
#include <bit>
#include <functional>
#include <robin_hood.h>

struct FastPairHash {
    static constexpr uint64_t k = 0x517cc1b727220a95;

    // For pair<uint16_t, uint16_t>
    size_t operator()(const std::pair<uint16_t, uint16_t>& p) const noexcept {
        // Pack the pair into a single 32-bit integer to hash
        uint32_t combined = (static_cast<uint32_t>(p.first) << 16) | p.second;
        return (combined * k) >> 32;
    }

    // For pair<uint64_t, uint8_t>
    size_t operator()(const std::pair<uint64_t, uint8_t>& p) const noexcept {
        // Combine the 64-bit value with the byte
        uint64_t combined = (p.first << 8) | p.second;
        return (combined * k) >> 32;
    }
};

template<typename V>
class LongestPrefixMatcher16 {
private:
    static constexpr uint64_t MASKS[] = {
        0x0000000000000000ULL, // 0 bytes
        0x00000000000000FFULL, // 1 byte
        0x000000000000FFFFULL, // 2 bytes
        0x0000000000FFFFFFULL, // 3 bytes
        0x00000000FFFFFFFFULL, // 4 bytes
        0x000000FFFFFFFFFFULL, // 5 bytes
        0x0000FFFFFFFFFFFFULL, // 6 bytes
        0x00FFFFFFFFFFFFFFULL, // 7 bytes
        0xFFFFFFFFFFFFFFFFULL  // 8 bytes
    };

    using Dictionary = robin_hood::unordered_map<std::pair<uint64_t, uint8_t>, V, FastPairHash>;
    using Bucket = std::vector<std::pair<std::pair<uint64_t, uint8_t>, V>>;
    using BucketMap =  robin_hood::unordered_map<uint64_t, Bucket>;

    Dictionary dictionary;
    BucketMap buckets;

    static inline uint64_t bytes_to_u64_le(const uint8_t* bytes, size_t len) {
        uint64_t value = *reinterpret_cast<const uint64_t*>(bytes);
        return value & MASKS[len];
    }

    static inline bool is_prefix(uint64_t text, uint64_t prefix, size_t text_size, size_t prefix_size) {
        return prefix_size <= text_size && shared_prefix_size(text, prefix) >= prefix_size;
    }

    static inline size_t shared_prefix_size(uint64_t a, uint64_t b) {
        return std::countr_zero(a ^ b) >> 3;
    }

public:
    LongestPrefixMatcher16() = default;

    inline void insert(const uint8_t* data, size_t length, V id) {
        if (length <= 8) {
            uint64_t value = bytes_to_u64_le(data, length);
            dictionary.emplace(std::make_pair(value, static_cast<uint8_t>(length)), id);
        } else {
            uint64_t prefix = bytes_to_u64_le(data, 8);
            size_t suffix_len = length - 8;
            uint64_t suffix = bytes_to_u64_le(data + 8, suffix_len);
            
            auto& bucket = buckets[prefix];
            bucket.emplace_back(
                std::make_pair(
                    std::make_pair(suffix, static_cast<uint8_t>(suffix_len)), 
                    id
                )
            );
            
            // Sort by suffix length in descending order
            std::sort(bucket.begin(), bucket.end(),
                [](const auto& a, const auto& b) {
                    return b.first.second < a.first.second;
                });
        }
    }

    inline std::optional<std::pair<V, size_t>> find_longest_match(const uint8_t* data, size_t length) const {
        // Long match handling
        if (length > 8) {
            size_t suffix_len = std::min(length, size_t{16}) - 8;
            uint64_t prefix = bytes_to_u64_le(data, 8);
            uint64_t suffix = bytes_to_u64_le(data + 8, suffix_len);
            
            auto bucket_it = buckets.find(prefix);
            if (bucket_it != buckets.end()) {
                const auto& bucket = bucket_it->second;
                for (const auto& entry : bucket) {
                    const auto& [entry_suffix, entry_suffix_len] = entry.first;
                    if (is_prefix(suffix, entry_suffix, suffix_len, entry_suffix_len)) {
                        return std::make_pair(entry.second, 8 + entry_suffix_len);
                    }
                }
            }
        }
        
        // Short match handling
        for (size_t len = std::min(length, size_t{8}); len > 0; --len) {
            uint64_t prefix = bytes_to_u64_le(data, len);
            auto it = dictionary.find(std::make_pair(prefix, static_cast<uint8_t>(len)));
            if (it != dictionary.end()) {
                return std::make_pair(it->second, len);
            }
        }
        
        return std::nullopt;
    }
};

#endif