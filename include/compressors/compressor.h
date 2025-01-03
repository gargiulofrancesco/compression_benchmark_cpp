#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include <cstddef>
#include <cstdint>
#include <vector>

template <typename Derived>
class Compressor {
public:
    // Static method to create instances of Derived class
    static Derived create(size_t data_size, size_t n_elements) {
        return Derived(data_size, n_elements);
    }

    void compress(const uint8_t* data, const std::vector<size_t>& end_positions) {
        static_cast<Derived*>(this)->compress(data, end_positions);
    }

    // Returns the number of bytes written in buffer
    size_t decompress(uint8_t* buffer) const {
        return static_cast<const Derived*>(this)->decompress(buffer);
    }

    // Returns the number of bytes written in buffer
    inline size_t get_item_at(size_t index, uint8_t* buffer) const {
        return static_cast<const Derived*>(this)->get_item_at(index, buffer);
    }

    size_t space_used_bytes() const {
        return static_cast<const Derived*>(this)->space_used_bytes();
    }

    const char* name() const {
        return static_cast<const Derived*>(this)->name();
    }
};

#endif // COMPRESSOR_H
