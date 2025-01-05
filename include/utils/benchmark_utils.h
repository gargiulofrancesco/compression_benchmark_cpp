#ifndef BENCHMARK_UTILS_H
#define BENCHMARK_UTILS_H

#include <string>
#include <vector>
#include <filesystem>

struct Dataset {
    std::string dataset_name;
    std::vector<std::string> data;
    std::vector<size_t> queries;
};

struct BenchmarkResult {
    std::string dataset_name;
    std::string compressor_name;
    double compression_rate;
    double compression_speed;
    double decompression_speed;
    double random_access_speed;
    double average_random_access_time;
};

Dataset load_dataset(const std::filesystem::path& path);
std::tuple<std::string, std::vector<uint8_t>, std::vector<size_t>, std::vector<size_t>> process_dataset(const Dataset& dataset);
std::vector<BenchmarkResult> read_benchmark_results(const std::filesystem::path& file_path);
void append_benchmark_result(const BenchmarkResult& result, const std::filesystem::path& output_file);
void print_benchmark_results(const std::vector<BenchmarkResult>& results);
void set_affinity(int core_id);

#endif // BENCHMARK_UTILS_H