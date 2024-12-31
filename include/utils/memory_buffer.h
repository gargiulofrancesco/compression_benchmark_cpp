#ifndef MEMORYBUFFER_H
#define MEMORYBUFFER_H

#include <cstddef>
#include <type_traits>
#include <stdexcept>

template<typename T>
class MemoryBuffer {
private:
    T* buffer;              // Pointer to the allocated memory
    size_t buf_capacity;    // Total capacity in elements
    size_t current_size;    // Current size in elements

public:
    // Constructor
    explicit MemoryBuffer(size_t capacity) 
        : buffer(new T[capacity])
        , buf_capacity(capacity)
        , current_size(0) 
    {}

    // Destructor
    inline ~MemoryBuffer() {
        delete[] buffer;
    }

    // Square bracket operators
    inline T& operator[](size_t index) {
        if (index >= current_size) {
            throw std::out_of_range("Index out of range");
        }
        return buffer[index];
    }

    inline const T& operator[](size_t index) const {
        if (index >= current_size) {
            throw std::out_of_range("Index out of range");
        }
        return buffer[index];
    }

    // Raw data access
    inline T* data() const {
        return buffer;
    }

    // Size operations
    inline size_t size() const {
        return current_size;
    }

    inline void set_size(size_t new_size) {
        if (new_size > buf_capacity) {
            throw std::length_error("New size exceeds capacity");
        }
        current_size = new_size;
    }

    inline size_t capacity() const {
        return buf_capacity;
    }

    inline void clear() {
        current_size = 0;
    }

    // Prevent copying
    MemoryBuffer(const MemoryBuffer&) = delete;
    MemoryBuffer& operator=(const MemoryBuffer&) = delete;

    // Allow moving
    MemoryBuffer(MemoryBuffer&& other) noexcept
        : buffer(other.buffer)
        , buf_capacity(other.buf_capacity)
        , current_size(other.current_size) 
    {
        other.buffer = nullptr;
        other.buf_capacity = 0;
        other.current_size = 0;
    }

    MemoryBuffer& operator=(MemoryBuffer&& other) noexcept {
        if (this != &other) {
            delete[] buffer;
            buffer = other.buffer;
            buf_capacity = other.buf_capacity;
            current_size = other.current_size;

            other.buffer = nullptr;
            other.buf_capacity = 0;
            other.current_size = 0;
        }
        return *this;
    }
};

#endif // MEMORYBUFFER_H