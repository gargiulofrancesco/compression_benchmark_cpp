#ifndef THRESHOLD_H
#define THRESHOLD_H

#include <cstdint>
#include <cmath>
#include <algorithm>

class Threshold {
private: 
    uint16_t threshold;           // The dynamic threshold value
    size_t target_sample_size;    // Target number of bytes to process before stopping
    size_t current_sample_size;   // Total bytes processed so far
    size_t tokens_to_insert;      // Number of tokens needed to fully populate the dictionary
    size_t update_period;         // How many token insertions before we update the threshold
    size_t current_update_merges; // Number of tokens inserted in the current update batch
    size_t current_update_bytes;  // Number of bytes processed in the current update batch

public:
    Threshold(size_t target_sample_size, size_t tokens_to_insert, size_t update_period)
        : threshold(1),
          target_sample_size(target_sample_size),
          current_sample_size(0),
          tokens_to_insert(tokens_to_insert),
          update_period(update_period),
          current_update_merges(0),
          current_update_bytes(0)
    {}

    inline uint16_t get() const {
        return threshold;
    }

    inline void update(size_t match_length, bool did_merge) {
        current_update_bytes += match_length;
        current_sample_size += match_length;

        if (did_merge) {
            tokens_to_insert--;
            current_update_merges++;

            if (current_update_merges == update_period) {
                size_t bytes_per_token = static_cast<size_t>(
                    std::ceil(static_cast<double>(current_update_bytes) / static_cast<double>(current_update_merges))
                );
                size_t predicted_missing_bytes = tokens_to_insert * bytes_per_token;
                size_t predicted_sample_size = current_sample_size + predicted_missing_bytes;

                if (predicted_sample_size > target_sample_size) {
                    threshold = (threshold > 1) ? threshold - 1 : 1;
                } else if (predicted_sample_size < target_sample_size) {
                    threshold = (threshold < UINT16_MAX - 1) ? threshold + 1 : UINT16_MAX - 1;
                }

                current_update_bytes = 0;
                current_update_merges = 0;
            }
        }
    }
};

#endif
