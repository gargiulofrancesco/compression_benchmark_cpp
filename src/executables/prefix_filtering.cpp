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
const int QUERIES_SEED = 123;

// Fixed seed and target sample fraction for OnPair's dictionary training
const size_t SAMPLING_SEED = 42;
const float TARGET_SAMPLE_FRACTION = 0.1f;

struct BenchmarkBatch {
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
     * 1. Pick a pivot and define a query prefix of length `prefix_size`.
     * 2. Scan the dataset to classify strings into buckets based on their LCP with the query (0 to prefix_size).
     * 3. Randomly sample candidates from buckets.
     */
    std::vector<BenchmarkBatch> generate_queries(
        size_t n_queries,
        size_t n_candidates,
        size_t prefix_size
    ) {
        std::vector<BenchmarkBatch> result;
        size_t n_strings = sorted_indices.size();

        // Randomize string order for selecting prefixes
        std::vector<size_t> prefix_sampling(n_strings);
        std::iota(prefix_sampling.begin(), prefix_sampling.end(), 0);
        std::shuffle(prefix_sampling.begin(), prefix_sampling.end(), rng);

        for(auto prefix_idx : prefix_sampling) {
            if(result.size() >= n_queries) break;

            std::string_view query_sv = getString(prefix_idx);
            
            if (query_sv.size() < prefix_size) {
                continue;
            }

            BenchmarkBatch batch;
            std::string_view prefix_sv = std::string_view(query_sv.data(), prefix_size);
            batch.prefix.assign(prefix_sv.begin(), prefix_sv.end());

            // Buckets[k] stores candidates that share exactly k bytes with prefix.
            std::vector<std::vector<size_t>> buckets(prefix_size + 1);
            for(size_t cand_idx = 0; cand_idx < n_strings; ++cand_idx) {
                if (cand_idx == prefix_idx) continue; // Skip the query itself
                std::string_view cand_sv = getString(cand_idx);
                size_t lcp = computeLCP(prefix_sv, cand_sv);
                buckets[lcp].push_back(cand_idx);
            }

            // Shuffle buckets to ensure randomness within same LCP
            for(auto& bucket : buckets) {
                std::shuffle(bucket.begin(), bucket.end(), rng);
            }

            // Strategy: Randomly sample candidates from buckets
            std::uniform_int_distribution <size_t> dist(0, prefix_size);
            while(batch.candidates.size() < n_candidates){
                size_t lcp_rnd = dist(rng);
                if(!buckets[lcp_rnd].empty()) {
                    batch.candidates.push_back(buckets[lcp_rnd].back());
                    buckets[lcp_rnd].pop_back();
                }
            }
            
            result.push_back(std::move(batch));
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
            size_t count_onpair = onpair.prefix_filtering_lazy(lpm, q.prefix, q.candidates, warm_buf.data());
            if (count_base != count_onpair) {
                std::cerr << "\n[FATAL] Warmup Count mismatch! Base=" << count_base << " OnPair=" << count_onpair << std::endl;
                exit(1);
            }
        }
    }

    // --- Benchmark prefix filtering ---
    std::vector<size_t> buffer_base(n_elements);
    std::vector<size_t> buffer_onpair(n_elements);

    for(size_t n_candidates = 128; n_candidates <= 8192; n_candidates *= 2) {
        
        // --- Output Header ---
        std::cout << std::endl;
        std::cout << "================================================================================" << std::endl;
        std::cout << "PREFIX FILTERING BENCHMARK" << std::endl;
        std::cout << "Dataset: " << dataset_name << " | #Candidates: " << n_candidates << " | Queries: " << N_QUERIES << std::endl;
        std::cout << "================================================================================" << std::endl;
        std::cout << std::left 
                << std::setw(18) << "Prefix (B)" 
                << std::setw(18) << "Baseline (ms)" 
                << std::setw(18) << "OnPair (ms)" 
                << std::setw(10) << "Speedup" 
                << std::endl;
        std::cout << "--------------------------------------------------------------------------------" << std::endl;
        
        for(size_t prefix_size = 4; prefix_size <= 64; prefix_size += 4) {
            // Generate queries
            auto queries = query_gen.generate_queries(N_QUERIES, n_candidates, prefix_size);
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
                size_t count_onpair = onpair.prefix_filtering_lazy(lpm, q.prefix, q.candidates, buffer_onpair.data());
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

            // Print Row
            std::cout << std::left 
                    << std::setw(18) << std::fixed << std::setprecision(2) << prefix_size
                    << std::setw(18) << std::fixed << std::setprecision(2) << base_ms 
                    << std::setw(18) << std::fixed << std::setprecision(2) << onpair_ms 
                    << "\033[1;32m" << std::setw(9) << speedup << "\033[0m" 
                    << std::endl;
        }
        std::cout << "================================================================================" << std::endl;
    }

    return 0;
}
