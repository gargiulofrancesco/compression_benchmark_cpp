/**
 * @file prefix_filtering.cpp
 * @brief Prefix Filtering Benchmark: Linear Scan vs. Compressed Domain Search.
 *
 * EVALUATION METHODOLOGY:
 * This benchmark evaluates the retrieval latency of prefix queries using a 
 * synthetic workload generated via Stratified Sampling. 
 *
 * * METRICS:
 * - Execution Time (ns): Accumulated latency over 10,000 queries.
 */

#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <filesystem>
#include <random>
#include <algorithm>
#include <map>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <numeric>
#include <set>
#include "benchmark_utils.h"
#include "onpair_compressor.h"
#include "raw_compressor.h"

// Number of runs for statistical significance
const size_t N_RUNS = 15;

// Queries configuration
const int N_QUERIES = 100;
const int QUERIES_SEED = 123;

// Fixed seed and target sample fraction for OnPair's dictionary training
const size_t SAMPLING_SEED = 42;
const float TARGET_SAMPLE_FRACTION = 0.1f;

class QueryGenerator {
    const std::vector<uint8_t>& data;
    const std::vector<size_t>& end_positions;
    std::mt19937 rng{QUERIES_SEED};

public:
    QueryGenerator(const std::vector<uint8_t>& data, 
                   const std::vector<size_t>& end_positions)
        : data(data), end_positions(end_positions) {}

    std::vector<std::vector<uint8_t>> generate_queries (size_t n_queries) {
        std::vector<std::vector<uint8_t>> result;
        size_t n_strings = end_positions.size() - 1;

        // Randomize string order for selecting prefixes
        std::vector<size_t> prefix_sampling(n_strings);
        std::iota(prefix_sampling.begin(), prefix_sampling.end(), 0);
        std::shuffle(prefix_sampling.begin(), prefix_sampling.end(), rng);

        for(auto prefix_idx : prefix_sampling) {
            if(result.size() >= n_queries) break;

            size_t string_start = end_positions[prefix_idx];
            size_t string_end = end_positions[prefix_idx + 1];
            size_t string_length = string_end - string_start;

            if (string_length == 0) continue;

            std::uniform_int_distribution<size_t> dist(1, string_length);
            size_t prefix_len = dist(rng);
            result.emplace_back(data.begin() + string_start, data.begin() + string_start + prefix_len);
        }

        return result;
    }
};

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
            std::cerr << "Warning: Failed to set CPU affinity. Continuing anyway.\n";
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

    // Dataset parameters
    size_t n_elements = end_positions.size() - 1;
    size_t data_size = data.size();

    // Latency measurements
    std::vector<size_t> baseline_latencies;
    std::vector<size_t> onpair_sorted_latencies;
    std::vector<size_t> onpair_unsorted_latencies;
    
    // Preallocate output buffers
    std::vector<size_t> buffer_base(n_elements);
    std::vector<size_t> buffer_onpair_sorted(n_elements);
    std::vector<size_t> buffer_onpair_unsorted(n_elements);

    // Run multiple times for statistical significance
    for(size_t run = 0; run < N_RUNS; ++run) {
        // Initialize Baseline Implementation (std::memcmp over uncompressed data)
        RawCompressor baseline(data_size, n_elements);
        baseline.compress(data.data(), end_positions);

        // Prepare OnPair Sorted compressor for prefix filtering
        OnPairCompressor onpair_sorted(data_size, n_elements);

        size_t target_sample_size = static_cast<size_t>(data.size() * TARGET_SAMPLE_FRACTION);
        auto [threshold, _] = find_onpair_params(data, end_positions, target_sample_size, SAMPLING_SEED);
        onpair_sorted.set_seed(SAMPLING_SEED);
        onpair_sorted.set_threshold(threshold);
        auto permutation = onpair_sorted.generate_random_permutation(n_elements);

        onpair_sorted.train_dictionary(data.data(), end_positions, permutation);
        auto lpm_sorted = onpair_sorted.sort_dictionary();
        onpair_sorted.parse_data(data.data(), end_positions, lpm_sorted);

        // Prepare OnPair Unsorted (control variable to evaluate speedup of sorted dictionary)
        OnPairCompressor onpair_unsorted(data_size, n_elements);
        onpair_unsorted.set_seed(SAMPLING_SEED);
        onpair_unsorted.set_threshold(threshold);
        auto lpm_unsorted = onpair_unsorted.train_dictionary(data.data(), end_positions, permutation).first;
        onpair_unsorted.parse_data(data.data(), end_positions, lpm_unsorted);

        // Initialize Query Generator
        QueryGenerator query_gen(data, end_positions);

        // --- Warmup Phase ---
        {
            std::vector<size_t> warm_buf(n_elements);
            auto warm_queries = query_gen.generate_queries(100);
            for(const auto& q : warm_queries) {
                size_t count_base = baseline.prefix_filtering(q, warm_buf.data());
                size_t count_onpair_sorted = onpair_sorted.prefix_filtering_sorted(lpm_sorted, q, warm_buf.data());
                size_t count_onpair_unsorted = onpair_unsorted.prefix_filtering_unsorted(lpm_unsorted, q, warm_buf.data());
                if (count_base != count_onpair_sorted || count_base != count_onpair_unsorted) {
                    std::cerr << "\n[FATAL] Warmup Count mismatch! Base=" << count_base << " OnPair Sorted=" << count_onpair_sorted << " OnPair Unsorted=" << count_onpair_unsorted << std::endl;
                    exit(1);
                }
            }
        }

        // Generate queries
        auto queries = query_gen.generate_queries(N_QUERIES);

        // Run benchmark
        for (const auto& q : queries) {
            // 1. Measure Baseline
            auto t0 = std::chrono::high_resolution_clock::now();
            size_t count_base = baseline.prefix_filtering(q, buffer_base.data());
            auto t1 = std::chrono::high_resolution_clock::now();
            baseline_latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());

            // 2. Measure OnPair Sorted
            auto t2 = std::chrono::high_resolution_clock::now();
            size_t count_onpair_sorted = onpair_sorted.prefix_filtering_sorted(lpm_sorted, q, buffer_onpair_sorted.data());
            auto t3 = std::chrono::high_resolution_clock::now();
            onpair_sorted_latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count());

            // 3. Measure OnPair Unsorted
            auto t4 = std::chrono::high_resolution_clock::now();
            size_t count_onpair_unsorted = onpair_unsorted.prefix_filtering_unsorted(lpm_unsorted, q, buffer_onpair_unsorted.data());
            auto t5 = std::chrono::high_resolution_clock::now();
            onpair_unsorted_latencies.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t5 - t4).count());

            // 4. Verify correctness
            if(count_base != count_onpair_sorted || count_base != count_onpair_unsorted) {
                std::cerr << "\n[FATAL] Count mismatch!" << std::endl;
                exit(1);
            }
            if(count_base > 0 && std::memcmp(buffer_base.data(), buffer_onpair_sorted.data(), count_base * sizeof(size_t)) != 0) {
                std::cerr << "\n[FATAL] Content mismatch!" << std::endl;
                exit(1);
            }
            if(count_base > 0 && std::memcmp(buffer_base.data(), buffer_onpair_unsorted.data(), count_base * sizeof(size_t)) != 0) {
                std::cerr << "\n[FATAL] Content mismatch!" << std::endl;
                exit(1);
            }
        }
    }

    // Compute average latencies
    double base_ns = std::accumulate(baseline_latencies.begin(), baseline_latencies.end(), 0.0) / baseline_latencies.size();
    double onpair_sorted_ns = std::accumulate(onpair_sorted_latencies.begin(), onpair_sorted_latencies.end(), 0.0) / onpair_sorted_latencies.size();
    double onpair_unsorted_ns = std::accumulate(onpair_unsorted_latencies.begin(), onpair_unsorted_latencies.end(), 0.0) / onpair_unsorted_latencies.size();

    // --- Output Header ---
    std::cout << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << "PREFIX FILTERING BENCHMARK" << std::endl;
    std::cout << "Dataset: " << dataset_name << " | Queries: " << N_QUERIES << "| Runs: " << N_RUNS << std::endl;
    std::cout << "================================================================" << std::endl;
    std::cout << std::left 
            << std::setw(18) << "Baseline (ms)" 
            << std::setw(20) << "OnPair Sorted (ms)" 
            << std::setw(22) << "OnPair Unsorted (ms)" 
            << std::endl;
    std::cout << "================================================================" << std::endl;
    
    // Print Row
    std::cout << std::left 
            << std::setw(18) << std::fixed << std::setprecision(2) << base_ns / 1e6 
            << std::setw(20) << std::fixed << std::setprecision(2) << onpair_sorted_ns / 1e6 
            << std::setw(22) << std::fixed << std::setprecision(2) << onpair_unsorted_ns / 1e6
            << std::endl;

    std::cout << "================================================================" << std::endl;

    return 0;
}
