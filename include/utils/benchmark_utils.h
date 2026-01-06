/**
 * Benchmark utilities and data structures for compression algorithm evaluation
 *
 * This module provides core infrastructure for systematic performance measurement
 * of string compression algorithms, including:
 * - Dataset loading and preprocessing
 * - Random query generation for access pattern simulation  
 * - Result aggregation and statistical analysis
 * - CPU affinity management for reproducible measurements (Linux only)
 */

#ifndef BENCHMARK_UTILS_H
#define BENCHMARK_UTILS_H

#include <string>
#include <vector>
#include <filesystem>

// Performance metrics for a single algorithm-dataset combination
struct BenchmarkResult {
    std::string dataset_name;
    std::string compressor_name;
    double compression_rate;                // Space reduction factor
    double compression_speed;               // Throughput in MiB/s
    double decompression_speed;             // Throughput in MiB/s
    size_t average_random_access_time;      // Latency in nanoseconds
};

/**
 * Loads and preprocesses JSON string datasets for benchmark evaluation
 * 
 * Expects JSON format: array of strings representing individual strings.
 * Returns flattened byte representation and positional metadata for efficient
 * random access during benchmark execution.
 *
 * @param path Path to the JSON dataset file
 * @return std::pair containing:
 *         - std::vector<uint8_t>: Concatenated string data as bytes
 *         - std::vector<size_t>: Boundary positions starting with 0, then cumulative string lengths.
 *           String i is located between end_positions[i] and end_positions[i+1].
 */
std::pair<std::vector<uint8_t>, std::vector<size_t>> load_dataset(const std::filesystem::path& path);

/**
 * Generates uniformly distributed random queries for access pattern simulation
 * 
 * Creates a representative workload for random access performance measurement.
 * Uniform distribution ensures unbiased latency assessment across all dataset strings.
 *
 * @param n Total number of strings in dataset
 * @param n_queries Number of random queries to generate
 * @return Vector of random indices within the range [0, n)
 */
std::vector<size_t> generate_random_queries(size_t n, size_t n_queries);

/**
 * Reads benchmark results from a JSON file
 * 
 * Loads previously saved benchmark results for analysis or continuation of benchmarking.
 * Returns empty vector if file doesn't exist or cannot be parsed.
 *
 * @param file_path Path to the JSON results file
 * @return Loaded benchmark results
 */
std::vector<BenchmarkResult> read_benchmark_results(const std::filesystem::path& file_path);

/**
 * Appends a new benchmark result to the results file
 * 
 * Reads existing results, appends the new result, and writes back to file.
 * Creates the file if it doesn't exist. Preserves all existing results.
 *
 * @param result The new benchmark result to append
 * @param output_file Path to the output JSON file
 */
void append_benchmark_result(const BenchmarkResult& result, const std::filesystem::path& output_file);

/**
 * Prints formatted benchmark results grouped by compressor
 * 
 * Groups results by compressor and dataset, calculates averages for each combination,
 * then displays results in a tabular format with overall averages per compressor.
 *
 * @param results Vector of benchmark results to display
 */
void print_benchmark_results(const std::vector<BenchmarkResult>& results);

/**
 * Determines parameters for OnPair compression to ensure fair comparison.
 * 
 * To compare OnPair and Sampled BPE fairly, we want both algorithms to use 
 * the same sample for dictionary training.
 * 
 * OnPair's training data size depends on its frequency threshold:
 * - Higher threshold -> slower dictionary filling -> more data processed
 * - Lower threshold -> faster dictionary filling -> less data processed
 * 
 * This function iterates through possible thresholds to find one that results
 * in a training sample size closest to target_sample_size (e.g., 10% of the dataset).
 * The resulting sample size can then used to configure Sampled BPE.
 */
std::pair<size_t, size_t> find_onpair_params(
    const std::vector<uint8_t>& data,
    const std::vector<size_t>& end_positions,
    const size_t target_sample_size,
    const size_t seed
);

/**
 * Attempts to set CPU affinity for reproducible measurements
 * 
 * Tries to bind the current process to a specific CPU core to reduce
 * measurement variance. Only supported on Linux systems.
 *
 * @param core_id The CPU core ID to bind to
 * @return true if CPU affinity was successfully set, false otherwise
 */
bool try_set_affinity(int core_id);

#endif // BENCHMARK_UTILS_H