#ifndef MEMORYBUFFER_H
#define MEMORYBUFFER_H

#include <cstdint>
#include <cstring>
#include <iostream>

class MemoryBuffer {
private:
    uint8_t* buffer;   // Pointer to the allocated memory
    size_t buf_capacity;   // Total capacity of the buffer
    size_t current_size; // Current size of the used data

public:
    // Constructor
    MemoryBuffer(size_t initial_capacity)
        : buffer(nullptr), buf_capacity(initial_capacity), current_size(0) {
        buffer = new uint8_t[buf_capacity];
    }

    // Destructor
    inline ~MemoryBuffer() {
        delete[] buffer;
    }

    // Square bracket operator for non-const access
    inline uint8_t& operator[](size_t index) {
        return buffer[index];
    }

    // Square bracket operator for const access
    inline const uint8_t& operator[](size_t index) const {
        return buffer[index];
    }

    // Access raw pointer
   inline uint8_t* data() const {
        return buffer;
    }

    // Get current size
    inline size_t size() const {
        return current_size;
    }

    // Set current size (does not ensure bounds)
    inline void set_size(size_t new_size) {
        current_size = new_size;
    }

    // Get total capacity
    inline size_t capacity() const {
        return buf_capacity;
    }

    // Clear buffer (reset size to 0)
    inline void clear() {
        current_size = 0;
    }

    // Prevent copy semantics (to avoid shallow copies)
    MemoryBuffer(const MemoryBuffer&) = delete;
    MemoryBuffer& operator=(const MemoryBuffer&) = delete;

    // Allow move semantics
    MemoryBuffer(MemoryBuffer&& other) noexcept
        : buffer(other.buffer), buf_capacity(other.buf_capacity), current_size(other.current_size) {
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
