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
 * - Speedup Factor: Baseline_Time / Compressed_Time.
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

// Queries configuration
const int N_QUERIES = 100;
const int N_CANDIDATES = 1000;
const int QUERIES_SEED = 123;

// Fixed seed and target sample fraction for OnPair's dictionary training
const size_t SAMPLING_SEED = 42;
const float TARGET_SAMPLE_FRACTION = 0.1f;

struct BenchmarkBatch {
    double lcp_avg;
    std::vector<uint8_t> prefix;
    std::vector<size_t> candidates;
};

class QueryGenerator {
    const std::vector<uint8_t>& data;
    const std::vector<size_t>& end_positions;
    std::vector<size_t> sorted_indices; // Permutation of string IDs
    std::mt19937 rng{QUERIES_SEED};

public:
    QueryGenerator(const std::vector<uint8_t>& data, 
                   const std::vector<size_t>& end_positions)
        : data(data), end_positions(end_positions) {
        
        // Preprocessing: Sort string indices lexicographically
        size_t n_strings = end_positions.size() - 1;
        sorted_indices.resize(n_strings);
        std::iota(sorted_indices.begin(), sorted_indices.end(), 0);

        std::sort(sorted_indices.begin(), sorted_indices.end(), 
            [&](size_t a, size_t b) {
                std::string_view sa = getString(a);
                std::string_view sb = getString(b);
                return sa < sb;
            });
    }

    /**
     * Strategy:
     * 1. Pick a pivot and define a query prefix of length `max_prefix_size`.
     * 2. Scan the dataset to classify strings into buckets based on their LCP with the query (0 to max).
     * 3. Sample candidates greedily from the highest LCP buckets to ensure hard negatives.
     */
    std::vector<BenchmarkBatch> generate_queries(
        size_t n_queries,
        size_t n_candidates,
        size_t max_prefix_size
    ) {
        std::vector<BenchmarkBatch> result;
        size_t n_strings = sorted_indices.size();

        // Use a randomized permutation for scanning to avoid locality bias
        std::vector<size_t> random_indices(n_strings);
        std::iota(random_indices.begin(), random_indices.end(), 0);
        std::shuffle(random_indices.begin(), random_indices.end(), rng);

        for(auto idx : random_indices) {
            if(result.size() >= n_queries) break;

            std::string_view query_sv = getString(idx);
            
            if (query_sv.size() < max_prefix_size) {
                continue;
            }

            BenchmarkBatch batch;
            std::string_view prefix_sv = std::string_view(query_sv.data(), max_prefix_size);
            batch.prefix.assign(prefix_sv.begin(), prefix_sv.end());

            // Buckets[k] stores candidates that share exactly k bytes with prefix.
            std::vector<std::vector<size_t>> buckets(max_prefix_size + 1);

            // Optimization: Cap bucket size to save memory. 
            // Since we scan in random order, filling the first items is a valid random sample
            size_t bucket_cap = n_candidates;
            size_t buckets_filled = 0;

            // Sample candidates
            std::vector<size_t> candidate_indices(n_strings);
            std::iota(candidate_indices.begin(), candidate_indices.end(), 0);
            std::shuffle(candidate_indices.begin(), candidate_indices.end(), rng);

            // Populate buckets
            for(auto cand_idx : candidate_indices) {
                if (cand_idx == idx) continue; // Skip the query itself

                std::string_view cand_sv = getString(cand_idx);
                size_t lcp = computeLCP(prefix_sv, cand_sv);

                if (buckets[lcp].size() < bucket_cap) {
                    buckets[lcp].push_back(cand_idx);
                    if (buckets[lcp].size() == bucket_cap) {
                        buckets_filled++;
                    }
                }

                if (buckets_filled == buckets.size()) {
                    break; // All buckets are full
                }
            }

            // Strategy: Prioritize Hard Negatives (Greedy Descending LCP).
            // Motivation: We construct a worst-case workload by filling the candidate set 
            // with strings sharing the longest possible prefix with the query.
            size_t total_lcp = 0;
            for(size_t lcp = max_prefix_size; lcp >= 0; --lcp) {
                while(!buckets[lcp].empty() && batch.candidates.size() < n_candidates) {
                    total_lcp += lcp;
                    batch.candidates.push_back(buckets[lcp].back());
                    buckets[lcp].pop_back();
                }
                if (batch.candidates.size() >= n_candidates) break;
            }

            double lcp_avg = static_cast<double>(total_lcp) / batch.candidates.size();
            double lcp_difference = static_cast<double>(max_prefix_size) - lcp_avg;
            double lcp_error = lcp_difference / static_cast<double>(max_prefix_size);

            // Accept only batches with sufficient candidates and low LCP error
            if(batch.candidates.size() == n_candidates && lcp_error <= 0.1) {
                // Shuffle final candidates to mix LCPs
                batch.lcp_avg = lcp_avg;
                std::shuffle(batch.candidates.begin(), batch.candidates.end(), rng);
                result.push_back(std::move(batch));
            }
        }

        return result;
    }

private:
    std::string_view getString(size_t index) const {
        size_t start = end_positions[index];
        size_t len = end_positions[index+1] - start;
        return {reinterpret_cast<const char*>(data.data() + start), len};
    }

    size_t computeLCP(std::string_view query, std::string_view candidate) const {
        size_t n = std::min(query.size(), candidate.size());
        size_t i = 0;
        while (i < n && query[i] == candidate[i]) {
            i++;
        }
        return i;
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
    
    size_t n_elements = end_positions.size() - 1;
    size_t data_size = data.size();

    // --- Output Header ---
    std::cout << "================================================================================" << std::endl;
    std::cout << "PREFIX FILTERING BENCHMARK" << std::endl;
    std::cout << "Dataset: " << dataset_name << " | Queries: " << N_QUERIES << std::endl;
    std::cout << "================================================================================" << std::endl;
    std::cout << std::left 
              << std::setw(18) << "LCP_avg" 
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

    // Initialize Baseline Implementation (std::memcmp over uncompressed data)
    RawCompressor baseline(data_size, n_elements);
    baseline.compress(data.data(), end_positions);

    // Initialize Query Generator
    QueryGenerator query_gen(data, end_positions);

    // --- Warmup Phase ---
    {
        std::vector<size_t> warm_buf(n_elements);
        auto warm_queries = query_gen.generate_queries(10, 100, 4);
        for(const auto& q : warm_queries) {
            size_t count_base = baseline.prefix_filtering(q.prefix, q.candidates, warm_buf.data());
            size_t count_onpair = onpair.prefix_filtering(lpm, q.prefix, q.candidates, warm_buf.data());
            if (count_base != count_onpair) {
                std::cerr << "\n[FATAL] Warmup Count mismatch! Base=" << count_base << " OnPair=" << count_onpair << std::endl;
                exit(1);
            }
        }
    }

    // --- Benchmark prefix filtering ---
    std::vector<size_t> buffer_base(n_elements);
    std::vector<size_t> buffer_onpair(n_elements);

    for(size_t min_prefix_size = 4; min_prefix_size <= 64; min_prefix_size += 4) {
        // Generate queries
        auto queries = query_gen.generate_queries(N_QUERIES, N_CANDIDATES, min_prefix_size);

        if(queries.size() < N_QUERIES) {
            break; // Not enough queries could be generated
        }
        
        size_t total_base_ns = 0;
        size_t total_onpair_ns = 0;

        for (const auto& q : queries) {
            // 1. Measure Baseline
            auto t0 = std::chrono::high_resolution_clock::now();
            size_t count_base = baseline.prefix_filtering(q.prefix, q.candidates, buffer_base.data());
            auto t1 = std::chrono::high_resolution_clock::now();
            total_base_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

            // 2. Measure OnPair
            auto t2 = std::chrono::high_resolution_clock::now();
            size_t count_onpair = onpair.prefix_filtering(lpm, q.prefix, q.candidates, buffer_onpair.data());
            auto t3 = std::chrono::high_resolution_clock::now();
            total_onpair_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count();

            // 3. Verify correctness
            if(count_base != count_onpair) {
                std::cerr << "\n[FATAL] Count mismatch!" << std::endl;
                exit(1);
            }
            if(count_base > 0 && std::memcmp(buffer_base.data(), buffer_onpair.data(), count_base * sizeof(size_t)) != 0) {
                 std::cerr << "\n[FATAL] Content mismatch!" << std::endl;
                 exit(1);
            }
        }
        
        // Calculate Metrics
        double base_ms = total_base_ns / 1e6;
        double onpair_ms = total_onpair_ns / 1e6;
        double speedup = base_ms / onpair_ms;
        double lcp_avg = std::accumulate(queries.begin(), queries.end(), 0.0,
            [](double sum, const auto& batch) { return sum + batch.lcp_avg; }) / queries.size();

        // Print Row
        std::cout << std::left 
                  << std::setw(18) << std::fixed << std::setprecision(2) << lcp_avg
                  << std::setw(18) << std::fixed << std::setprecision(2) << base_ms 
                  << std::setw(18) << std::fixed << std::setprecision(2) << onpair_ms 
                  << "\033[1;32m" << std::setw(9) << speedup << "x\033[0m" 
                  << std::endl;
    }

    std::cout << "================================================================================" << std::endl;

    return 0;
}
