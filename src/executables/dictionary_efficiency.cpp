/**
 * @file dictionary_efficiency.cpp
 * @brief Benchmark comparing OnPair and Sampled BPE on the same sample size.
 *
 * This experiment measures training speed and compression ratio for both algorithms
 * using identical data samples. The sample size is determined by OnPair's training
 * requirements at different frequency thresholds, and then applied to Sampled BPE
 * for a fair comparison.
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <string>
#include <filesystem>
#include <random>
#include <algorithm>
#include <numeric>
#include <optional>
#include "onpair_compressor.h"
#include "sampled_bpe_compressor.h"
#include "benchmark_utils.h"

const int SEED = 42;
const int MAX_THRESHOLD = 15;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <dataset_path> [core_id]" << std::endl;
        return 1;
    }

    std::string dataset_path = argv[1];
    std::optional<int> core_id = std::nullopt;
    if (argc > 2) {
        try {
            core_id = std::stoi(argv[2]);
        } catch (const std::exception&) {
            std::cerr << "Error: Invalid core_id '" << argv[2] << "'. Must be a valid number.\n";
            return 1;
        }
    }

    // Set CPU affinity if specified
    if (core_id.has_value()) {
        if (!try_set_affinity(core_id.value())) {
            std::cerr << "Warning: Failed to set CPU affinity to core " << core_id.value() 
                      << ". Continuing without core pinning.\n";
        }
    }

    // Load dataset
    std::string dataset_name = std::filesystem::path(dataset_path).filename().string();
    std::vector<uint8_t> data;
    std::vector<size_t> end_positions;

    try {
        auto result = load_dataset(dataset_path);
        data = std::move(result.first);
        end_positions = std::move(result.second);
    } catch (const std::exception& e) {
        std::cerr << "Error loading dataset: " << e.what() << std::endl;
        return 1;
    }

    size_t n_elements = end_positions.size() - 1;
    size_t data_size = data.size();

    std::cout << "Dataset: " << dataset_name << " (" << data_size << " bytes)" << std::endl << std::endl;

    // Create compressors for warmup and permutation generation
    OnPairCompressor onpair_warmup(data_size, n_elements);
    SampledBPECompressor sampled_bpe_warmup(data_size, n_elements);

    // Set fixed seed for permutation
    onpair_warmup.set_seed(SEED);
    sampled_bpe_warmup.set_seed(SEED);

    // Generate permutation for the full dataset with fixed seed
    std::vector<int> permutation = onpair_warmup.generate_random_permutation(n_elements);

    // Warmup phase
    {
        // Warmup OnPair
        onpair_warmup.set_threshold(MAX_THRESHOLD);
        auto [lpm_warmup, warmup_sample_size] = onpair_warmup.train_dictionary(data.data(), end_positions, permutation);
        onpair_warmup.parse_data(data.data(), end_positions, lpm_warmup);
        
        // Warmup Sampled BPE
        sampled_bpe_warmup.set_sample_size(warmup_sample_size);
        auto [sampled_data_warmup, sampled_end_positions_warmup] = sampled_bpe_warmup.sampling(data.data(), end_positions);
        auto lpm_bpe_warmup = sampled_bpe_warmup.train_dictionary(sampled_data_warmup.data(), sampled_end_positions_warmup);
        sampled_bpe_warmup.parse_data(data.data(), end_positions, lpm_bpe_warmup);
    }

    std::cout << std::left 
              << std::setw(12) << "Threshold" 
              << std::setw(22) << "Sample Size (B)" 
              << std::setw(12) << "Sample %"
              << std::setw(22) << "OnPair Time (s)" 
              << std::setw(18) << "OnPair Ratio" 
              << std::setw(22) << "BPE Time (s)" 
              << std::setw(18) << "BPE Ratio" 
              << std::endl;

    for (size_t threshold = 2; threshold <= MAX_THRESHOLD; ++threshold) {
        // Create fresh compressors for each iteration
        OnPairCompressor onpair(data_size, n_elements);
        SampledBPECompressor sampled_bpe(data_size, n_elements);

        // Set fixed seed for permutation
        onpair.set_seed(SEED);
        sampled_bpe.set_seed(SEED);

        // Set threshold for OnPair
        onpair.set_threshold(threshold);

        // --- OnPair ---
        // Measure OnPair training time
        auto start_onpair = std::chrono::high_resolution_clock::now();
        auto [lpm_onpair, onpair_sample_size] = onpair.train_dictionary(data.data(), end_positions, permutation);
        auto end_onpair = std::chrono::high_resolution_clock::now();
        double onpair_time = std::chrono::duration_cast<std::chrono::duration<double>>(end_onpair - start_onpair).count();

        // Measure OnPair compression ratio
        onpair.parse_data(data.data(), end_positions, lpm_onpair);
        size_t onpair_size = onpair.space_used_bytes();
        double onpair_ratio = static_cast<double>(data_size) / static_cast<double>(onpair_size);

        // --- Sampled BPE ---
        // Generate sample using the same sample as OnPair
        sampled_bpe.set_sample_size(onpair_sample_size);
        auto [sampled_data, sampled_end_positions] = sampled_bpe.sampling(data.data(), end_positions);

        // Measure Sampled BPE training time
        auto start_bpe = std::chrono::high_resolution_clock::now();
        auto lpm_bpe = sampled_bpe.train_dictionary(sampled_data.data(), sampled_end_positions);
        auto end_bpe = std::chrono::high_resolution_clock::now();
        double bpe_time = std::chrono::duration_cast<std::chrono::duration<double>>(end_bpe - start_bpe).count();

        // Measure Sampled BPE compression ratio
        sampled_bpe.parse_data(data.data(), end_positions, lpm_bpe);
        size_t bpe_size = sampled_bpe.space_used_bytes();
        double bpe_ratio = static_cast<double>(data_size) / static_cast<double>(bpe_size);

        double sample_percentage = (static_cast<double>(onpair_sample_size) / data_size) * 100.0;

        std::cout << std::left 
                  << std::setw(12) << threshold 
                  << std::setw(22) << onpair_sample_size 
                  << std::setw(12) << std::fixed << std::setprecision(2) << sample_percentage
                  << std::setw(22) << std::fixed << std::setprecision(6) << onpair_time 
                  << std::setw(18) << std::fixed << std::setprecision(4) << onpair_ratio 
                  << std::setw(22) << std::fixed << std::setprecision(6) << bpe_time 
                  << std::setw(18) << std::fixed << std::setprecision(4) << bpe_ratio 
                  << std::endl;
    }

    return 0;
}
