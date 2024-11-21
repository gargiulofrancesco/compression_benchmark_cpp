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

    // Base class method calls the corresponding method in the Derived class using static_cast
    void compress(const std::vector<uint8_t>& data, const std::vector<size_t>& end_positions) {
        static_cast<Derived*>(this)->compress(data, end_positions);
    }

    void decompress(std::vector<uint8_t>& buffer) const {
        static_cast<const Derived*>(this)->decompress(buffer);
    }

    void get_item_at(size_t index, std::vector<uint8_t>& buffer) const {
        static_cast<const Derived*>(this)->get_item_at(index, buffer);
    }

    size_t space_used_bytes() const {
        return static_cast<const Derived*>(this)->space_used_bytes();
    }

    const char* name() const {
        return static_cast<const Derived*>(this)->name();
    }
};

#endif // COMPRESSOR_H
