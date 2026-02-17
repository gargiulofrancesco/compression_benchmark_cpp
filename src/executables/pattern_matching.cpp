/**
 * @file pattern_matching.cpp
 * @brief Benchmark for Pattern Matching (LIKE '%P%'): KMP on Raw vs. Compressed Domain
 *
 * This benchmark evaluates the efficiency of substring pattern matching queries
 * on string datasets using two approaches:
 *   1. Baseline: Standard KMP over uncompressed data (RawCompressor).
 *   2. OnPair: KMP-based compressed domain pattern matching exploiting
 *      precomputed Base/Bridge tables over the sorted dictionary.
 *
 * Output:
 *   - Reports the average latency (in milliseconds) for each method across all queries and runs.
 *   - Reports the average preprocessing time for OnPair's table construction.
 *
 * Usage:
 *   ./pattern_matching <dataset_path> [core_id]
 *
 *   - <dataset_path>: Path to the input dataset (JSON format).
 *   - [core_id]: (Optional) Pin the process to a specific CPU core.
 */

#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <filesystem>
#include <random>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <numeric>
#include "benchmark_utils.h"
#include "onpair_compressor.h"
#include "raw_compressor.h"

// Number of runs for statistical significance
const size_t N_RUNS = 5;

// Queries configuration
const int N_QUERIES = 100;
const int QUERIES_SEED = 456;
const size_t MIN_PATTERN_LEN = 3;
const size_t MAX_PATTERN_LEN = 20;

// Fixed seed and target sample fraction for OnPair's dictionary training
const size_t SAMPLING_SEED = 42;
const float TARGET_SAMPLE_FRACTION = 0.1f;

class PatternGenerator {
    const std::vector<uint8_t>& data;
    const std::vector<size_t>& end_positions;
    std::mt19937 rng{QUERIES_SEED};

public:
    PatternGenerator(const std::vector<uint8_t>& data, 
                     const std::vector<size_t>& end_positions)
        : data(data), end_positions(end_positions) {}

    std::vector<std::vector<uint8_t>> generate_patterns(size_t n_queries) {
        std::vector<std::vector<uint8_t>> result;
        size_t n_strings = end_positions.size() - 1;

        // Randomize string order for selecting patterns
        std::vector<size_t> sampling(n_strings);
        std::iota(sampling.begin(), sampling.end(), 0);
        std::shuffle(sampling.begin(), sampling.end(), rng);

        for (auto str_idx : sampling) {
            if (result.size() >= n_queries) break;

            size_t string_start = end_positions[str_idx];
            size_t string_end = end_positions[str_idx + 1];
            size_t string_length = string_end - string_start;

            if (string_length < MIN_PATTERN_LEN) continue;

            // Pick a random substring within [MIN_PATTERN_LEN, min(MAX_PATTERN_LEN, string_length)]
            size_t max_len = std::min(MAX_PATTERN_LEN, string_length);
            std::uniform_int_distribution<size_t> len_dist(MIN_PATTERN_LEN, max_len);
            size_t pattern_len = len_dist(rng);

            // Random start offset within the string
            std::uniform_int_distribution<size_t> start_dist(0, string_length - pattern_len);
            size_t offset = start_dist(rng);

            result.emplace_back(
                data.begin() + string_start + offset,
                data.begin() + string_start + offset + pattern_len
            );
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

    // Latency measurements (nanoseconds)
    std::vector<size_t> baseline_latencies;
    std::vector<size_t> onpair_latencies;
    std::vector<size_t> onpair_preprocess_latencies;

    // Preallocate output buffers
    std::vector<size_t> buffer_base(n_elements);
    std::vector<size_t> buffer_onpair(n_elements);

    // Initialize Baseline (KMP over uncompressed data)
    RawCompressor baseline(data_size, n_elements);
    baseline.compress(data.data(), end_positions);

    // Initialize OnPair with sorted dictionary
    OnPairCompressor onpair(data_size, n_elements);

    size_t target_sample_size = static_cast<size_t>(data.size() * TARGET_SAMPLE_FRACTION);
    auto [threshold, _] = find_onpair_params(data, end_positions, target_sample_size, SAMPLING_SEED);
    onpair.set_seed(SAMPLING_SEED);
    onpair.set_threshold(threshold);
    auto permutation = onpair.generate_random_permutation(n_elements);

    onpair.train_dictionary(data.data(), end_positions, permutation);
    auto lpm = onpair.sort_dictionary();
    onpair.parse_data(data.data(), end_positions, lpm);

    // Initialize Pattern Generator
    PatternGenerator pattern_gen(data, end_positions);

    // --- Warmup Phase ---
    {
        std::vector<size_t> warm_buf(n_elements);
        auto warm_patterns = pattern_gen.generate_patterns(50);
        for (const auto& p : warm_patterns) {
            auto tables = onpair.build_pattern_match_tables(p);
            size_t count_base = baseline.pattern_matching(p, warm_buf.data());
            size_t count_onpair = onpair.pattern_matching(tables, warm_buf.data());
            if (count_base != count_onpair) {
                std::cerr << "\n[FATAL] Warmup Count mismatch! Base=" << count_base 
                          << " OnPair=" << count_onpair << std::endl;
                std::cerr << "Pattern (len=" << p.size() << "): ";
                for (auto b : p) std::cerr << static_cast<char>(b);
                std::cerr << std::endl;
                exit(1);
            }
        }
    }

    // Run multiple times for statistical significance
    for (size_t run = 0; run < N_RUNS; ++run) {
        std::cout << "Run " << (run + 1) << "/" << N_RUNS << "..." << std::endl;

        // Generate patterns
        auto patterns = pattern_gen.generate_patterns(N_QUERIES);

        // Run benchmark
        for (const auto& p : patterns) {            
            // 1. Measure OnPair preprocessing (table construction)
            auto tp0 = std::chrono::high_resolution_clock::now();
            auto tables = onpair.build_pattern_match_tables(p);
            auto tp1 = std::chrono::high_resolution_clock::now();
            onpair_preprocess_latencies.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(tp1 - tp0).count()
            );

            // 2. Measure Baseline (KMP on uncompressed data)
            auto t0 = std::chrono::high_resolution_clock::now();
            size_t count_base = baseline.pattern_matching(p, buffer_base.data());
            auto t1 = std::chrono::high_resolution_clock::now();
            baseline_latencies.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()
            );

            // 3. Measure OnPair (compressed domain KMP scan only)
            auto t2 = std::chrono::high_resolution_clock::now();
            size_t count_onpair = onpair.pattern_matching(tables, buffer_onpair.data());
            auto t3 = std::chrono::high_resolution_clock::now();
            onpair_latencies.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count()
            );

            // 4. Verify correctness
            if (count_base != count_onpair) {
                std::cerr << "\n[FATAL] Count mismatch! Base=" << count_base 
                          << " OnPair=" << count_onpair << std::endl;
                std::cerr << "Pattern (len=" << p.size() << "): ";
                for (auto b : p) std::cerr << static_cast<char>(b);
                std::cerr << std::endl;
                exit(1);
            }
            if (count_base > 0 && std::memcmp(buffer_base.data(), buffer_onpair.data(), count_base * sizeof(size_t)) != 0) {
                std::cerr << "\n[FATAL] Content mismatch!" << std::endl;
                exit(1);
            }
        }
    }

    // Compute average latencies
    double base_ns = std::accumulate(baseline_latencies.begin(), baseline_latencies.end(), 0.0) / baseline_latencies.size();
    double onpair_ns = std::accumulate(onpair_latencies.begin(), onpair_latencies.end(), 0.0) / onpair_latencies.size();
    double onpair_preprocess_ns = std::accumulate(onpair_preprocess_latencies.begin(), onpair_preprocess_latencies.end(), 0.0) / onpair_preprocess_latencies.size();

    // --- Output ---
    std::cout << std::endl;
    std::cout << "===================================================================================" << std::endl;
    std::cout << "PATTERN MATCHING BENCHMARK (LIKE '%P%')" << std::endl;
    std::cout << "Dataset: " << dataset_name 
              << " | Queries: " << N_QUERIES 
              << " | Runs: " << N_RUNS 
              << " | Pattern len: [" << MIN_PATTERN_LEN << ", " << MAX_PATTERN_LEN << "]" << std::endl;
    std::cout << "===================================================================================" << std::endl;
    std::cout << std::left
              << std::setw(20) << "Baseline (ms)"
              << std::setw(22) << "OnPair Total (ms)"
              << std::setw(24) << "OnPair Preproc (ms)"
              << std::setw(22) << "OnPair Scan (ms)"
              << std::endl;
    std::cout << "===================================================================================" << std::endl;

    std::cout << std::left
              << std::setw(20) << std::fixed << std::setprecision(2) << base_ns / 1e6
              << std::setw(22) << std::fixed << std::setprecision(2) << (onpair_ns + onpair_preprocess_ns) / 1e6
              << std::setw(24) << std::fixed << std::setprecision(2) << onpair_preprocess_ns / 1e6
              << std::setw(22) << std::fixed << std::setprecision(2) << onpair_ns / 1e6
              << std::endl;

    std::cout << "===================================================================================" << std::endl;

    return 0;
}
