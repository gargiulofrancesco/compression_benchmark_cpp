#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>
#include "benchmark_utils.h"

const std::vector<std::string> COMPRESSORS = {"copy", "lz4", "snappy", "zstd", "fsst", "onpair16"};
const std::string BENCHMARK_PATH = "./benchmark_individual";
const std::string OUTPUT_FILE = "benchmark_results.json";
const size_t N_ITERATIONS = 15;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        throw std::runtime_error("Error: Missing directory argument. Usage: " + std::string(argv[0]) + ": " + " <directory>\n");
    }

    std::filesystem::path directory(argv[1]);

    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        throw std::runtime_error("Error: " + directory.string() + " is not a valid directory.\n");
    }

    if (std::filesystem::exists(OUTPUT_FILE)) {
        std::filesystem::remove(OUTPUT_FILE);
    }

    for (const auto& entry: std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::string dataset_path = entry.path().string();
            std::cout << "Processing dataset \"" << dataset_path << "\"\n";

            for (const auto& compressor: COMPRESSORS) {
                for (size_t i = 0; i < N_ITERATIONS; ++i) {
                    int status = std::system((BENCHMARK_PATH + " " + dataset_path + " " + compressor + " " + OUTPUT_FILE).c_str());
                    if (status != 0) {
                        throw std::runtime_error("Benchmark failed for dataset '" + dataset_path + "' with compressor '" + compressor + "'.\n");
                    }
                }
            }
        }
    }

    auto results = read_benchmark_results(OUTPUT_FILE);
    print_benchmark_results(results);

    return 0;
}
