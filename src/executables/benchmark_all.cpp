//! Comprehensive benchmark for string compression algorithms
//! 
//! This binary orchestrates systematic evaluation of compression algorithms for
//! string collections with random access requirements. The benchmark suite measures:
//! - Compression ratio and throughput
//! - Decompression throughput  
//! - Random access latency
//!
//! Each algorithm is evaluated across N_ITERATIONS runs for statistical significance.
//! Results are aggregated and persisted in JSON format for further analysis.

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>
#include <optional>
#include "benchmark_utils.h"

/// Compression algorithms under evaluation
const std::vector<std::string> COMPRESSORS = {"raw", "onpair", "onpair16"};
/// Path to individual benchmark executable
const std::string BENCHMARK_PATH = "./benchmark_individual";
/// Output file for aggregated benchmark results
const std::string OUTPUT_FILE = "benchmark_results.json";
/// Number of iterations per algorithm-dataset combination for statistical robustness
const size_t N_ITERATIONS = 15;

/// Main benchmark orchestrator
/// 
/// Executes comprehensive evaluation of compression algorithms across all JSON datasets
/// in the specified directory. For each dataset-algorithm pair, performs N_ITERATIONS
/// independent measurements to ensure statistical significance.
int main(int argc, char* argv[]) {
    // Parse command-line arguments: dataset directory and optional CPU core ID
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <directory> [core_id]\n";
        std::cerr << "  <directory>  - Directory containing JSON dataset files\n";
        std::cerr << "  [core_id]    - Optional CPU core ID for pinning\n";
        return 1;
    }

    std::filesystem::path directory(argv[1]);
    // Optional CPU core affinity for consistent performance measurements
    std::optional<int> core_id = std::nullopt;
    if (argc > 2) {
        try {
            core_id = std::stoi(argv[2]);
        } catch (const std::exception&) {
            std::cerr << "Error: Invalid core_id '" << argv[2] << "'. Must be a valid number.\n";
            return 1;
        }
    }

    // Validate dataset directory
    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        std::cerr << "Error: " << directory.string() << " is not a valid directory.\n";
        return 1;
    }

    // Initialize clean results file for this benchmark run
    if (std::filesystem::exists(OUTPUT_FILE)) {
        std::filesystem::remove(OUTPUT_FILE);
    }

    // Systematic evaluation across all datasets and compression algorithms
    for (const auto& entry: std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::string dataset_path = entry.path().string();
            std::cout << "Processing dataset \"" << dataset_path << "\"\n";

            // Evaluate each compression algorithm
            for (const auto& compressor: COMPRESSORS) {
                std::cout << "- " << compressor << "\n";
                // Multiple iterations for statistical robustness
                for (size_t i = 0; i < N_ITERATIONS; ++i) {
                    // Execute individual benchmark with specified parameters
                    std::string command = BENCHMARK_PATH + " " + dataset_path + " " + compressor + " " + OUTPUT_FILE;
                    
                    // Apply CPU core affinity if specified
                    if (core_id.has_value()) {
                        command += " " + std::to_string(core_id.value());
                    }
                    
                    int status = std::system(command.c_str());
                    
                    if (status != 0) {
                        std::cerr << "Benchmark failed for dataset '" << dataset_path 
                                  << "' with compressor '" << compressor << "'.\n";
                        return 1;
                    }
                }
            }
        }
    }

    // Generate comprehensive benchmark report
    auto results = read_benchmark_results(OUTPUT_FILE);
    print_benchmark_results(results);

    return 0;
}
