/**
 * Individual benchmark executor for compression algorithm evaluation
 *
 * This binary performs isolated performance measurement of a single compression algorithm
 * on a single dataset. Metrics collected include:
 * - Compression ratio and throughput (MiB/s)
 * - Decompression throughput (MiB/s) 
 * - Random access latency (ns)
 *
 * Results are appended to a JSON file for later aggregation and analysis.
 */

#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>
#include <stdexcept>
#include <optional>
#include "raw_compressor.h"
#include "fsst_compressor.h"
#include "fsst_block_compressor.h"
#include "lz4_compressor.h"
#include "snappy_compressor.h"
#include "xz_compressor.h"
#include "brotli_compressor.h"
#include "deflate_compressor.h"
#include "zstd_compressor.h"
#include "onpair_compressor.h"
#include "deflate_individual_compressor.h"
#include "bpe_compressor.h"
#include "sampled_bpe_compressor.h"
#include "benchmark_utils.h"

// Number of random access queries for latency measurement
const size_t N_QUERIES = 1000000;

// Fixed seed and target sample fraction for dictionary training (OnPair and Sampled BPE)
const size_t SAMPLING_SEED = 42;
const float TARGET_SAMPLE_FRACTION = 0.1f;

/**
 * Core benchmark function implementing the measurement protocol
 * 
 * Executes the complete evaluation pipeline:
 * 1. Compression phase with timing measurement
 * 2. Full decompression with validation and timing  
 * 3. Random access evaluation over N_QUERIES uniformly distributed queries
 * 4. Data integrity verification at each step
 *
 * @param compressor The compression algorithm instance to benchmark
 * @param dataset_name Name of the dataset being evaluated
 * @param data Raw byte data from the dataset
 * @param end_positions Boundary positions for individual strings in the data
 * @param queries Vector of random indices for access pattern simulation
 * @return BenchmarkResult containing aggregated performance metrics for statistical analysis
 */
template <typename CompressorType>
BenchmarkResult benchmark(CompressorType& compressor,
                          const std::string& dataset_name,
                          const std::vector<uint8_t>& data,
                          const std::vector<size_t>& end_positions,
                          const std::vector<size_t>& queries) {
    std::vector<uint8_t> buffer(data.size() + 1024);
    const double data_bytes = static_cast<double>(data.size());

    // Initialize benchmark result
    BenchmarkResult result;
    result.dataset_name = dataset_name;
    result.compressor_name = compressor.name();

    // Phase 1: Compression measurement
    auto start_compression = std::chrono::high_resolution_clock::now();
    compressor.compress(data.data(), end_positions);
    auto end_compression = std::chrono::high_resolution_clock::now();

    double compression_time = std::chrono::duration_cast<std::chrono::duration<double>>(end_compression - start_compression).count();
    result.compression_rate = data_bytes / static_cast<double>(compressor.space_used_bytes());
    result.compression_speed = (data_bytes / (1024.0 * 1024.0)) / compression_time;

    // Phase 2: Decompression measurement with validation
    auto start_decompression = std::chrono::high_resolution_clock::now();
    size_t b_size = compressor.decompress(buffer.data());
    auto end_decompression = std::chrono::high_resolution_clock::now();
    
    // Verify decompression correctness
    if (!std::equal(data.data(), data.data() + data.size(), buffer.data())) {
        throw std::runtime_error("Data mismatch during decompression for compressor: " + std::string(compressor.name()));
    }
    double decompression_time = std::chrono::duration_cast<std::chrono::duration<double>>(end_decompression - start_decompression).count();
    result.decompression_speed = (data_bytes / (1024.0 * 1024.0)) / decompression_time;

    // Phase 3: Random access latency measurement
    size_t total_random_access_time = 0;
    for (size_t query : queries) {
        size_t start_position = end_positions[query];
        size_t end_position = end_positions[query+1];
        size_t item_size = end_position - start_position;
        
        auto start_random_access = std::chrono::high_resolution_clock::now();
        size_t b_size = compressor.get_item_at(query, buffer.data());
        auto end_random_access = std::chrono::high_resolution_clock::now();
        
        // Verify random access correctness
        if (!std::equal(data.data() + start_position, data.data() + end_position, buffer.data())) {
            throw std::runtime_error("Data mismatch during random access for compressor: " + std::string(compressor.name()));
        }
        size_t random_access_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_random_access - start_random_access).count();
        total_random_access_time += random_access_time;
    }

    result.average_random_access_time = total_random_access_time / queries.size();

    return result;
}

/**
 * Warmup function to prepare CPU caches and branch predictors
 * 
 * Does not record metrics but ensures the system is in a steady state
 * before the actual measurement begins.
 */
template <typename CompressorType>
void warmup(const std::string& dataset_name,
            const std::vector<uint8_t>& data,
            const std::vector<size_t>& end_positions,
            const std::vector<size_t>& queries) {    
    // Limit warmup to ~256KiB
    const size_t TARGET_WARMUP_SIZE = 256 * 1024;
    
    // Find the first position where cumulative size >= TARGET_WARMUP_SIZE using binary search
    auto it = std::lower_bound(end_positions.begin(), end_positions.end(), TARGET_WARMUP_SIZE);
    size_t limit_idx = std::distance(end_positions.begin(), it);
    
    // Ensure we don't exceed the available data
    limit_idx = std::min(limit_idx, end_positions.size() - 1);
    
    size_t warmup_data_size = end_positions[limit_idx];
    std::vector<size_t> warmup_end_positions(end_positions.begin(), end_positions.begin() + limit_idx + 1);

    // Create a temporary compressor for warmup
    CompressorType compressor = CompressorType::create(warmup_data_size, limit_idx);
    std::vector<uint8_t> buffer(warmup_data_size + 1024);

    // Dummy variable to prevent compiler optimizations
    size_t dummy = 0;

    // Phase 1: Compression
    compressor.compress(data.data(), warmup_end_positions);
    dummy += compressor.space_used_bytes();

    // Phase 2: Decompression
    dummy += compressor.decompress(buffer.data());

    // Phase 3: Random access (subset of queries)
    size_t n_warmup_queries = std::min(size_t(10000), queries.size() - 1);
    for (size_t i = 0; i < n_warmup_queries; ++i) {
       dummy += compressor.get_item_at(queries[i] % limit_idx, buffer.data());
    }

    // Prevent compiler from optimizing away the warmup code
    if (dummy == 0) {
        std::cout << "Warmup dummy value: " << dummy << std::endl;
    }
}

// Individual benchmark execution entry point
int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <dataset_path> <compressor_name> <output_file> [core_id]\n";
        return 1;
    }

    std::filesystem::path dataset_path(argv[1]);
    std::string compressor_name(argv[2]);
    std::filesystem::path output_file(argv[3]);
    std::optional<int> core_id = std::nullopt;
    if (argc > 4) {
        try {
            core_id = std::stoi(argv[4]);
        } catch (const std::exception&) {
            std::cerr << "Error: Invalid core_id '" << argv[4] << "'. Must be a valid number.\n";
            return 1;
        }
    }

    // Validate dataset path
    if (!std::filesystem::exists(dataset_path)) {
        std::cerr << "Error: Dataset path '" << dataset_path << "' does not exist.\n";
        return 1;
    }
    if (!std::filesystem::is_regular_file(dataset_path)) {
        std::cerr << "Error: Dataset path '" << dataset_path << "' is not a file.\n";
        return 1;
    }

    // Set CPU affinity if specified
    if (core_id.has_value()) {
        if (!try_set_affinity(core_id.value())) {
            std::cerr << "Warning: Failed to set CPU affinity to core " << core_id.value() 
                      << ". Continuing without core pinning.\n";
        }
    }

    try {
        // Load dataset
        std::string dataset_name = dataset_path.filename().string();
        auto [data, end_positions] = load_dataset(dataset_path);
        size_t n_elements = end_positions.size() - 1;
        std::vector<size_t> queries = generate_random_queries(n_elements, N_QUERIES);

        // Initialize the compressor
        BenchmarkResult result;
        if (compressor_name == "raw") {
            warmup<RawCompressor>(dataset_name, data, end_positions, queries);
            RawCompressor compressor = RawCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "lz4") {
            warmup<LZ4Compressor>(dataset_name, data, end_positions, queries);
            LZ4Compressor compressor = LZ4Compressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "snappy") {
            warmup<SnappyCompressor>(dataset_name, data, end_positions, queries);
            SnappyCompressor compressor = SnappyCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "xz") {
            warmup<XZCompressor>(dataset_name, data, end_positions, queries);
            XZCompressor compressor = XZCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "zstd") {
            warmup<ZstdCompressor>(dataset_name, data, end_positions, queries);
            ZstdCompressor compressor = ZstdCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "deflate") {
            warmup<DeflateCompressor>(dataset_name, data, end_positions, queries);
            DeflateCompressor compressor = DeflateCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "brotli") {
            warmup<BrotliCompressor>(dataset_name, data, end_positions, queries);
            BrotliCompressor compressor = BrotliCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "fsst") {
            warmup<FSSTCompressor>(dataset_name, data, end_positions, queries);
            FSSTCompressor compressor = FSSTCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        } 
        else if (compressor_name == "fsst_block") {
            warmup<FSSTBlockCompressor>(dataset_name, data, end_positions, queries);
            FSSTBlockCompressor compressor = FSSTBlockCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "onpair") {
            warmup<OnPairCompressor>(dataset_name, data, end_positions, queries);
            size_t target_sample_size = static_cast<size_t>(data.size() * TARGET_SAMPLE_FRACTION);
            auto [threshold, _] = find_onpair_params(data, end_positions, target_sample_size, SAMPLING_SEED);
            OnPairCompressor compressor = OnPairCompressor::create(data.size(), end_positions.size()-1);
            compressor.set_seed(SAMPLING_SEED);
            compressor.set_threshold(threshold);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "deflate_individual") {
            warmup<DeflateIndividualCompressor>(dataset_name, data, end_positions, queries);
            DeflateIndividualCompressor compressor = DeflateIndividualCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "bpe") {
            warmup<BPECompressor>(dataset_name, data, end_positions, queries);
            BPECompressor compressor = BPECompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "sampled_bpe") {
            warmup<SampledBPECompressor>(dataset_name, data, end_positions, queries);
            size_t target_sample_size = static_cast<size_t>(data.size() * TARGET_SAMPLE_FRACTION);
            auto [_, sample_size] = find_onpair_params(data, end_positions, target_sample_size, SAMPLING_SEED);
            SampledBPECompressor compressor = SampledBPECompressor::create(data.size(), end_positions.size()-1);
            compressor.set_seed(SAMPLING_SEED);
            compressor.set_sample_size(sample_size);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else {
            std::cerr << "Unknown compressor: " << compressor_name << "\n";
            return 1;
        }

        // Append the result to the output file
        append_benchmark_result(result, output_file);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
