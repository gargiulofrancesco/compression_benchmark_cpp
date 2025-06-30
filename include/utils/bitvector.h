#ifndef BITVECTOR_H
#define BITVECTOR_H

#include <vector>
#include <cstdint>
#include <cassert>
#include <optional>
#include <limits>
#include <climits>

class BitVector {
    private:
        std::vector<uint64_t> data;
        size_t position = 0;

        static constexpr uint64_t all_ones = ~0ULL;

        class OnesIterator {
            private:
                const BitVector& bv;
                size_t pos;
                size_t max;
            
            public:
                OnesIterator(const BitVector& bv_ref, size_t start)
                    : bv(bv_ref), pos(start), max(bv_ref.len()) {
                    next();
                }

                size_t operator*() const { return pos; }

                OnesIterator& operator++() {
                    ++pos;
                    next();
                    return *this;
                }

                OnesIterator operator++(int) {
                    OnesIterator temp = *this;
                    ++(*this);
                    return temp;
                }

                bool operator==(const OnesIterator& other) const {
                    return pos == other.pos;
                }

                bool operator!=(const OnesIterator& other) const {
                    return pos != other.pos;
                }

            private:
                void next() {
                    while (pos < max) {
                        size_t word_index = pos / 64;
                        size_t bit_offset = pos % 64;

                        if (word_index >= bv.data.size()) {
                            pos = max;
                            return;
                        }

                        uint64_t word = bv.data[word_index] >> bit_offset;

                        if (word != 0) {
                            pos += __builtin_ctzll(word);
                            return;
                        } else {
                            pos += 64 - bit_offset;
                        }
                    }
                }
        };

    public:
        BitVector() = default;

        OnesIterator ones_begin() const { return OnesIterator(*this, 0); }
        OnesIterator ones_end() const { return OnesIterator(*this, len()); }

        static BitVector with_capacity(size_t n_bits) {
            BitVector bv;
            bv.data.reserve((n_bits + 63) / 64);
            return bv;
        }

        static BitVector with_zeroes(size_t n_bits) {
            BitVector bv = with_capacity(n_bits);
            bv.extend_with_zeroes(n_bits);
            bv.shrink_to_fit();
            return bv;
        }

        static BitVector with_ones(size_t n_bits) {
            BitVector bv = with_capacity(n_bits);
            bv.extend_with_ones(n_bits);
            bv.shrink_to_fit();
            return bv;
        }

        inline void extend_with_zeroes(size_t n) {
            position += n;
            size_t new_size = (position + 63) / 64;
            data.resize(new_size, 0);
        }

        inline void extend_with_ones(size_t n) {
            position += n;
            size_t new_size = (position + 63) / 64;
            size_t old_size = data.size();
            data.resize(new_size, all_ones);
            if (position % 64 != 0) {
                size_t rem = position % 64;
                data[new_size - 1] = (1ULL << rem) - 1;
            }
        }

        inline void push(bool bit) {
            size_t pos_in_word = position % 64;
            if (pos_in_word == 0)
                data.push_back(0);

            if (bit)
                data.back() |= (1ULL << pos_in_word);

            position++;
        }

        inline std::optional<bool> get(size_t index) const {
            if (index >= position)
                return std::nullopt;

            size_t word = index >> 6;
            size_t pos = index & 63;
            return (data[word] >> pos) & 1;
        }

        inline void set(size_t index, bool bit) {
            size_t word = index >> 6;
            size_t pos = index & 63;
            data[word] &= ~(1ULL << pos);
            data[word] |= (static_cast<uint64_t>(bit) << pos);
        }

        inline void append_bits(uint64_t bits, size_t len) {
            assert(len <= 64);
            assert(len == 64 || (bits >> len) == 0);

            if (len == 0) return;

            size_t pos_in_word = position % 64;
            position += len;

            if (pos_in_word == 0) {
                data.push_back(bits);
            } else {
                data.back() |= bits << pos_in_word;
                if (len > 64 - pos_in_word) {
                    data.push_back(bits >> (64 - pos_in_word));
                }
            }
        }

        inline std::optional<uint64_t> get_bits(size_t index, size_t len) const {
            if (len > 64 || index + len > position)
                return std::nullopt;

            if (len == 0)
                return 0;

            size_t block = index >> 6;
            size_t shift = index & 63;

            uint64_t mask = (len == 64) ? all_ones : ((1ULL << len) - 1);

            if (shift + len <= 64)
                return (data[block] >> shift) & mask;
            else
                return ((data[block] >> shift) | (data[block + 1] << (64 - shift))) & mask;
        }

        inline std::optional<size_t> next_one(size_t pos) const {
            size_t next_pos = pos + 1;
            size_t word_pos = next_pos >> 6;
            if (word_pos >= data.size()) return std::nullopt;

            uint64_t buffer = data[word_pos] >> (next_pos % 64);

            while (buffer == 0) {
                next_pos += 64 - (next_pos % 64);
                word_pos = next_pos >> 6;
                if (word_pos >= data.size()) return std::nullopt;
                buffer = data[word_pos];
            }

            return next_pos + __builtin_ctzll(buffer);
        }

        inline std::optional<size_t> prev_one(size_t pos) const {
            if (pos == 0) return std::nullopt;

            size_t prev_pos = pos - 1;
            size_t word_pos = prev_pos >> 6;

            uint64_t buffer = data[word_pos] << (63 - (prev_pos % 64));

            while (buffer == 0) {
                if (word_pos == 0) return std::nullopt;
                word_pos--;
                prev_pos = (word_pos + 1) * 64 - 1;
                buffer = data[word_pos];
            }

            return prev_pos - __builtin_clzll(buffer);
        }

        void shrink_to_fit() {
            data.shrink_to_fit();
        }

        bool is_empty() const {
            return position == 0;
        }

        size_t len() const {
            return position;
        }

        const std::vector<uint64_t>& raw_data() const {
            return data;
        }
};

#endif // BITVECTOR_H