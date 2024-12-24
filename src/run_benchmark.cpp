#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdlib>
#include "fsst_compressor.h"
#include "dataset_loader.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

const std::vector<std::string> COMPRESSORS = {"copy", "fsst"};
const std::string BENCHMARK_PATH = "./run_single_benchmark";
const std::string OUTPUT_FILE = "benchmark_results.json";
const size_t N_ITERATIONS = 15;

void print_benchmark_results(const std::vector<BenchmarkResult>& results) {
    // Group results by (compressor, dataset) pair
    std::map<std::pair<std::string, std::string>, std::vector<BenchmarkResult>> grouped_results;
    for (const auto& result : results) {
        grouped_results[{result.compressor_name, result.dataset_name}].push_back(result);
    }

    // Calculate averages and group by compressor
    std::map<std::string, std::vector<BenchmarkResult>> compressor_groups;
    std::map<std::string, BenchmarkResult> compressor_averages;

    for (const auto& [key, group] : grouped_results) {
        const auto& [compressor, dataset] = key;
        size_t len = group.size();
        double avg_compression_rate = 0, avg_compression_speed = 0, avg_decompression_speed = 0,
               avg_random_access_speed = 0, avg_average_random_access_time = 0;

        for (const auto& result : group) {
            avg_compression_rate += result.compression_rate;
            avg_compression_speed += result.compression_speed;
            avg_decompression_speed += result.decompression_speed;
            avg_random_access_speed += result.random_access_speed;
            avg_average_random_access_time += result.average_random_access_time;
        }

        BenchmarkResult averaged_result = {
            dataset,
            compressor,
            avg_compression_rate / len,
            avg_compression_speed / len,
            avg_decompression_speed / len,
            avg_random_access_speed / len,
            avg_average_random_access_time / len
        };
        compressor_groups[compressor].push_back(averaged_result);
    }

    // Calculate overall averages for each compressor
    for (const auto& [compressor, results] : compressor_groups) {
        size_t len = results.size();
        double overall_avg_compression_rate = 0, overall_avg_compression_speed = 0, 
               overall_avg_decompression_speed = 0, overall_avg_random_access_speed = 0, 
               overall_avg_random_access_time = 0;

        for (const auto& result : results) {
            overall_avg_compression_rate += result.compression_rate;
            overall_avg_compression_speed += result.compression_speed;
            overall_avg_decompression_speed += result.decompression_speed;
            overall_avg_random_access_speed += result.random_access_speed;
            overall_avg_random_access_time += result.average_random_access_time;
        }

        compressor_averages[compressor] = {
            "AVERAGE",
            compressor,
            overall_avg_compression_rate / len,
            overall_avg_compression_speed / len,
            overall_avg_decompression_speed / len,
            overall_avg_random_access_speed / len,
            overall_avg_random_access_time / len
        };
    }

    // Print grouped and averaged results
    for (const auto& [compressor, results] : compressor_groups) {
        std::cout << "\nResults for Compressor: " << compressor << "\n";
        std::cout << std::setw(20) << "Dataset"
                  << std::setw(12) << "C. Rate"
                  << std::setw(20) << "C. Speed (MB/s)"
                  << std::setw(20) << "D. Speed (MB/s)"
                  << std::setw(20) << "R.A. Speed (MB/s)"
                  << std::setw(20) << "R.A. Time (ns)" << "\n";

        for (const auto& result : results) {
            std::cout << std::setw(20) << result.dataset_name
                      << std::setw(12) << std::fixed << std::setprecision(3) << result.compression_rate
                      << std::setw(20) << std::fixed << std::setprecision(2) << result.compression_speed
                      << std::setw(20) << std::fixed << std::setprecision(2) << result.decompression_speed
                      << std::setw(20) << std::fixed << std::setprecision(2) << result.random_access_speed
                      << std::setw(20) << std::fixed << std::setprecision(0) << (result.average_random_access_time * 1e9) << "\n";
        }

        // Print the overall averages row
        const auto& avg_result = compressor_averages[compressor];
        std::cout << std::setw(20) << avg_result.dataset_name
                  << std::setw(12) << std::fixed << std::setprecision(3) << avg_result.compression_rate
                  << std::setw(20) << std::fixed << std::setprecision(2) << avg_result.compression_speed
                  << std::setw(20) << std::fixed << std::setprecision(2) << avg_result.decompression_speed
                  << std::setw(20) << std::fixed << std::setprecision(2) << avg_result.random_access_speed
                  << std::setw(20) << std::fixed << std::setprecision(0) << (avg_result.average_random_access_time * 1e9) << "\n";
    }
}

std::vector<BenchmarkResult> read_results(const std::string& file_path) {
    std::vector<BenchmarkResult> results;

    if (fs::exists(file_path)) {
        std::ifstream file(file_path);
        if (file.is_open()) {
            try {
                json j;
                file >> j;
                results = j.get<std::vector<BenchmarkResult>>();
            } catch (const json::exception& e) {
                std::cerr << "Error parsing results file '" << file_path << "': " << e.what() << std::endl;
            }
        }
    }
    return results;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Error: Missing directory argument. Usage: " << argv[0] << " <directory>\n";
        return 1;
    }

    fs::path directory(argv[1]);

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        std::cerr << "Error: " << directory << " is not a valid directory.\n";
        return 1;
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
                        std::cerr << "Benchmark failed for dataset '" << dataset_path << "' with compressor '" << compressor << "'.\n";
                    }
                }
            }
        }
    }

    auto results = read_results(OUTPUT_FILE);
    print_benchmark_results(results);

    return 0;
}
