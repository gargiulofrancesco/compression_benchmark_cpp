#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>
#include "benchmark_utils.h"

struct Compressor {
    std::string name;
    std::vector<int> compression_levels;
};

const std::vector<Compressor> COMPRESSORS = {
    {"deflate", {1, 6, 9}},
    {"brotli", {1, 3, 6}},
    {"zstd", {1, 3, 6, 9, 12}},
    {"lz4", {0, 1, 3, 6, 9, 12}},
    {"snappy", {0}},
    {"xz", {1, 3}},
};
const std::string BENCHMARK_PATH = "./estimate_compressibility_individual";
const std::string OUTPUT_FILE = "compressibility_estimate_results.json";
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
                for(int level : compressor.compression_levels) {
                    for (size_t i = 0; i < N_ITERATIONS; ++i) {
                        int status = std::system((BENCHMARK_PATH + " " + dataset_path + " " + compressor.name + " " + std::to_string(level) + " " + OUTPUT_FILE).c_str());
                        if (status != 0) {
                            throw std::runtime_error("Benchmark failed for dataset '" + dataset_path + "' with compressor '" + compressor.name + "'.\n");
                        }
                    }
                }
            }
        }
    }

    auto results = read_benchmark_results(OUTPUT_FILE);
    print_benchmark_results(results);

    return 0;
}
