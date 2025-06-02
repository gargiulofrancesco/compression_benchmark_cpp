#ifndef BENCHMARK_UTILS_H
#define BENCHMARK_UTILS_H

#include <string>
#include <vector>
#include <filesystem>

struct BenchmarkResult {
    std::string dataset_name;
    std::string compressor_name;
    double compression_rate;
    double compression_speed; // in MiB/s
    double decompression_speed; // in MiB/s
    size_t average_random_access_time; // in nanoseconds
};

std::pair<std::vector<uint8_t>, std::vector<size_t>> load_dataset(const std::filesystem::path& path);
std::vector<size_t> generate_random_queries(size_t n, size_t N_QUERIES);
std::vector<BenchmarkResult> read_benchmark_results(const std::filesystem::path& file_path);
void append_benchmark_result(const BenchmarkResult& result, const std::filesystem::path& output_file);
void print_benchmark_results(const std::vector<BenchmarkResult>& results);
void set_affinity(int core_id);

#endif // BENCHMARK_UTILS_H