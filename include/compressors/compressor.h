#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * Base template class for compression algorithm implementations using CRTP
 *
 * This abstract base provides a uniform interface for all compression algorithms
 * in the benchmark framework. Uses the Curiously Recurring Template Pattern (CRTP)
 * to enable static polymorphism for performance-critical operations.
 *
 * All derived classes must implement:
 * - Compression/decompression of string collections
 * - Access to individual strings by index
 * - Space usage reporting for compression ratio calculation
 */
template <typename Derived>
class Compressor {
public:
    /**
     * Factory method to create instances of derived compression algorithms
     *
     * @param data_size Total size of input data in bytes
     * @param n_elements Number of individual strings in the dataset
     * @return New instance of the derived compressor class
     */
    static Derived create(size_t data_size, size_t n_elements) {
        return Derived(data_size, n_elements);
    }

    /**
     * Compresses the input dataset using the derived algorithm
     *
     * @param data Concatenated string data as byte array
     * @param end_positions Boundary positions for individual strings (cumulative lengths)
     */
    void compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
        static_cast<Derived*>(this)->compress(data, end_positions);
    }

    /**
     * Decompresses the entire dataset to provided buffer
     *
     * @param buffer Output buffer for decompressed data (must be pre-allocated)
     * @return Number of bytes written to the buffer
     */
    size_t decompress(uint8_t* buffer) const {
        return static_cast<const Derived*>(this)->decompress(buffer);
    }

    /**
     * Retrieves a single string by index
     *
     * @param index Zero-based index of the string to retrieve
     * @param buffer Output buffer for the decompressed string
     * @return Number of bytes written to the buffer
     */
    inline size_t get_item_at(size_t index, uint8_t* buffer) const {
        return static_cast<const Derived*>(this)->get_item_at(index, buffer);
    }

    /**
     * Reports total memory usage of the compressed representation
     *
     * @return Total bytes used by compressed data and metadata structures
     */
    size_t space_used_bytes() const {
        return static_cast<const Derived*>(this)->space_used_bytes();
    }

    /**
     * Returns the human-readable name of the compression algorithm
     *
     * @return Identifier for the algorithm (e.g., "lz4", "zstd")
     */
    const char* name() const {
        return static_cast<const Derived*>(this)->name();
    }
};

#endif // COMPRESSOR_H
