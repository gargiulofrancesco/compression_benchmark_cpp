#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>
#include <simdjson.h>
#include "dataset_loader.h"

using namespace simdjson;
namespace fs = std::filesystem;

const std::vector<std::string> COMPRESSORS = {"copy", "fsst", "snappy", "zstd", "deflate", "lz4", "onpair16"};
const std::string BENCHMARK_PATH = "./run_single_benchmark";
const std::string OUTPUT_FILE = "benchmark_results.json";
const size_t N_ITERATIONS = 15;

std::vector<BenchmarkResult> read_results(const std::string& file_path) {
    std::vector<BenchmarkResult> results;

    if (fs::exists(file_path)) {
        try {
            ondemand::parser parser;
            padded_string json = padded_string::load(file_path);
            ondemand::document doc = parser.iterate(json);
            
            for (ondemand::object result : doc.get_array()) {
                BenchmarkResult br;
                br.dataset_name = std::string(result["dataset_name"].get_string().value());
                br.compressor_name = std::string(result["compressor_name"].get_string().value());
                br.compression_rate = result["compression_rate"].get_double().value();
                br.compression_speed = result["compression_speed"].get_double().value();
                br.decompression_speed = result["decompression_speed"].get_double().value();
                br.random_access_speed = result["random_access_speed"].get_double().value();
                br.average_random_access_time = result["average_random_access_time"].get_double().value();
                results.push_back(br);
            }
        } catch (const simdjson::simdjson_error& e) {
            throw std::runtime_error("Error parsing results file '" + file_path + ": " + e.what());
        }
    }
    return results;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        throw std::runtime_error("Error: Missing directory argument. Usage: " + std::string(argv[0]) + ": " + " <directory>\n");
    }

    fs::path directory(argv[1]);

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        throw std::runtime_error("Error: " + directory.string() + " is not a valid directory.\n");
    }

    if (fs::exists(OUTPUT_FILE)) {
        fs::remove(OUTPUT_FILE);
    }

    for (const auto& entry: fs::directory_iterator(directory)) {
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

    auto results = read_results(OUTPUT_FILE);
    print_benchmark_results(results);

    return 0;
}
