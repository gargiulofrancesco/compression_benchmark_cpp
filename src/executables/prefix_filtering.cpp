/**
 * @file prefix_filtering.cpp
 * @brief Prefix Filtering Benchmark: Linear Scan vs. Compressed Domain Search.
 *
 * EVALUATION METHODOLOGY:
 * This benchmark evaluates the retrieval latency of prefix queries using a 
 * synthetic workload generated via Stratified Sampling. The workload consists of:
 * - 1. Positive Queries (50%): Stratified by selectivity (Rare vs. Frequent)
 * - 2. Negative Queries (50%): "Near-miss" prefixes (mutated valid strings) that 
 * do not exist in the datase.
 *
 * * METRICS:
 * - Execution Time (ns): Accumulated latency over 10,000 queries.
 * - Speedup Factor: Baseline_Time / Compressed_Time.
 */

#include <iostream>
#include <vector>
#include <string>
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

// Queries configuration
const int N_QUERIES = 100;
const int QUERIES_SEED = 123;
const std::vector<int> PREFIXES = {4, 8, 12, 16};

// Fixed seed and target sample fraction for OnPair's dictionary training
const size_t SAMPLING_SEED = 42;
const float TARGET_SAMPLE_FRACTION = 0.1f;

/**
 * Generates queries using stratified sampling based on prefix selectivity.
 *
 * Creates a balanced dataset of:
 * 1. Positive Queries (Hits): Stratified by selectivity (Rare vs. Frequent).
 * 2. Negative Queries (Misses): Mutated prefixes guaranteed NOT to exist.
 *
 * @return A vector of byte-vectors representing the queries.
 */
std::vector<std::vector<uint8_t>> generate_stratified_queries(
    const std::vector<uint8_t>& data,
    const std::vector<size_t>& end_positions,
    size_t prefix_size,
    size_t n_queries_target
) {
    std::mt19937 rng(QUERIES_SEED); 

    // --- Step 1: Extract and Index All Valid Prefixes ---
    std::map<std::vector<uint8_t>, size_t> prefix_counts;
    std::set<std::vector<uint8_t>> unique_prefixes;

    size_t n_strings = end_positions.size() - 1;
    for (size_t i = 0; i < n_strings; ++i) {
        size_t start = end_positions[i];
        size_t len = end_positions[i+1] - start;
        
        if (len >= prefix_size) {
            std::vector<uint8_t> prefix(data.begin() + start, data.begin() + start + prefix_size);
            prefix_counts[prefix]++;
            unique_prefixes.insert(prefix);
        }
    }

    if (unique_prefixes.empty()) return {};

    // --- Step 2: Stratified Sampling for POSITIVE Queries (50% of workload) ---
    size_t n_positives = n_queries_target / 2;
    std::vector<std::vector<uint8_t>> positive_queries;

    std::map<size_t, std::vector<std::vector<uint8_t>>> buckets;
    for (const auto& [prefix, count] : prefix_counts) {
        size_t bucket_idx = 0;
        size_t c = count;
        while (c >>= 1) bucket_idx++; // log2
        buckets[bucket_idx].push_back(prefix);
    }

    // Sample uniformly from buckets to ensure coverage of both Rare and Frequent terms
    if (!buckets.empty()) {
        size_t queries_per_bucket = std::max(size_t(1), n_positives / buckets.size());
        for (auto& [b_idx, p_list] : buckets) {
            std::shuffle(p_list.begin(), p_list.end(), rng);
            size_t take = std::min(p_list.size(), queries_per_bucket);
            positive_queries.insert(positive_queries.end(), p_list.begin(), p_list.begin() + take);
        }
    }
    
    // Fill remaining spots if bucket sampling didn't reach target
    std::vector<std::vector<uint8_t>> all_valid_list(unique_prefixes.begin(), unique_prefixes.end());
    std::uniform_int_distribution<size_t> dist_idx(0, all_valid_list.size() - 1);
    while (positive_queries.size() < n_positives) {
        positive_queries.push_back(all_valid_list[dist_idx(rng)]);
    }

    // --- Step 3: Generation of NEGATIVE Queries (50% of workload) ---
    // We take valid prefixes and mutate the last byte to create realistic "near-misses".
    size_t n_negatives = n_queries_target - positive_queries.size();
    std::vector<std::vector<uint8_t>> negative_queries;
    
    int attempts = 0;
    while (negative_queries.size() < n_negatives && attempts < n_queries_target * 5) {
        // Pick a valid prefix to mutate
        std::vector<uint8_t> candidate = all_valid_list[dist_idx(rng)];
        
        // Mutate last byte (simple heuristic)
        // We try +1, -1, or random byte to simulate typos or non-existent IDs
        uint8_t mutation = rng() % 256; 
        candidate.back() = mutation;

        // CRITICAL: Verify it is actually a miss
        if (unique_prefixes.find(candidate) == unique_prefixes.end()) {
            negative_queries.push_back(candidate);
        }
        attempts++;
    }

    // --- Step 4: Merge and Shuffle ---
    std::vector<std::vector<uint8_t>> final_workload = positive_queries;
    final_workload.insert(final_workload.end(), negative_queries.begin(), negative_queries.end());
    std::shuffle(final_workload.begin(), final_workload.end(), rng);

    return final_workload;
}

/**
 * Prefix filtering using a linear scan over the dataset.
 * 
 * For each string in the dataset:
 * 1. Check if the string length is at least as long as the prefix.
 * 2. Perform a memcmp to compare the prefix with the start of the string.
 * 3. If it matches, record the string's index in the buffer.
 * 
 * Returns the number of matches found.
 */
size_t linear_scan_prefix_filtering(
    const std::vector<uint8_t>& data,
    const std::vector<size_t>& end_positions,
    const std::vector<uint8_t>& prefix,
    size_t* buffer
) {
    size_t count = 0;
    size_t n_strings = end_positions.size() - 1;
    size_t p_len = prefix.size();
    const uint8_t* p_data = prefix.data();

    for (size_t i = 0; i < n_strings; ++i) {
        size_t start = end_positions[i];
        size_t len = end_positions[i + 1] - start;

        // Length Filter
        if (len < p_len) continue;

        // Memcmp Comparison
        bool match = std::memcmp(&data[start], p_data, p_len) == 0;
        buffer[count] = i;
        count += match ? 1 : 0;
    }

    return count;
}

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

    // --- Output Header ---
    std::cout << "================================================================================" << std::endl;
    std::cout << "PREFIX FILTERING BENCHMARK" << std::endl;
    std::cout << "Dataset: " << dataset_name << " | Queries: " << N_QUERIES << " (50% Hits / 50% Misses)" << std::endl;
    std::cout << "================================================================================" << std::endl;
    std::cout << std::left 
              << std::setw(10) << "Prefix" 
              << std::setw(18) << "Baseline (ms)" 
              << std::setw(18) << "OnPair (ms)" 
              << std::setw(10) << "Speedup" << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;

    // Prepare OnPair compressor for prefix filtering
    OnPairCompressor onpair(data_size, n_elements);

    size_t target_sample_size = static_cast<size_t>(data.size() * TARGET_SAMPLE_FRACTION);
    auto [threshold, _] = find_onpair_params(data, end_positions, target_sample_size, SAMPLING_SEED);
    onpair.set_seed(SAMPLING_SEED);
    onpair.set_threshold(threshold);
    auto permutation = onpair.generate_random_permutation(n_elements);

    onpair.train_dictionary(data.data(), end_positions, permutation);
    auto lpm = onpair.sort_dictionary();
    onpair.parse_data(data.data(), end_positions, lpm);

    // --- Warmup Phase ---
    {
        std::vector<size_t> warm_buf(n_elements);
        auto warm_queries = generate_stratified_queries(data, end_positions, 4, 100);
        for(const auto& q : warm_queries) {
            size_t count_base = linear_scan_prefix_filtering(data, end_positions, q, warm_buf.data());
            size_t count_onpair = onpair.prefix_filtering(lpm, q, warm_buf.data());
            if (count_base != count_onpair) {
                std::cerr << "\n[FATAL] Warmup Count mismatch! Base=" << count_base << " OnPair=" << count_onpair << std::endl;
                exit(1);
            }
        }
    }

    // --- Benchmark prefix filtering ---
    std::vector<size_t> buffer_base(n_elements);
    std::vector<size_t> buffer_onpair(n_elements);

    for(size_t prefix_size : PREFIXES) {
        // Generate queries
        auto queries = generate_stratified_queries(data, end_positions, prefix_size, N_QUERIES);
        
        size_t total_base_ns = 0;
        size_t total_onpair_ns = 0;

        for (const auto& q : queries) {
            // 1. Measure Baseline
            auto t0 = std::chrono::high_resolution_clock::now();
            size_t count_base = linear_scan_prefix_filtering(data, end_positions, q, buffer_base.data());
            auto t1 = std::chrono::high_resolution_clock::now();
            total_base_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

            // 2. Measure OnPair
            auto t2 = std::chrono::high_resolution_clock::now();
            size_t count_onpair = onpair.prefix_filtering(lpm, q, buffer_onpair.data());
            auto t3 = std::chrono::high_resolution_clock::now();
            total_onpair_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count();

            // 3. Verify correctness
            if(count_base != count_onpair) {
                std::cerr << "\n[FATAL] Count mismatch! P=" << prefix_size << " Base=" << count_base << " OnPair=" << count_onpair << std::endl;
                exit(1);
            }
            if(count_base > 0 && std::memcmp(buffer_base.data(), buffer_onpair.data(), count_base * sizeof(size_t)) != 0) {
                 std::cerr << "\n[FATAL] Content mismatch! P=" << prefix_size << std::endl;
                 exit(1);
            }
        }
        
        // Calculate Metrics
        double base_ms = total_base_ns / 1e6;
        double onpair_ms = total_onpair_ns / 1e6;
        double speedup = base_ms / onpair_ms;

        // Print Row
        std::cout << std::left 
                  << std::setw(10) << prefix_size 
                  << std::setw(18) << std::fixed << std::setprecision(2) << base_ms 
                  << std::setw(18) << std::fixed << std::setprecision(2) << onpair_ms 
                  << "\033[1;32m" << std::setw(9) << speedup << "x\033[0m" 
                  << std::endl;
    }

    std::cout << "================================================================================" << std::endl;

    return 0;
}
