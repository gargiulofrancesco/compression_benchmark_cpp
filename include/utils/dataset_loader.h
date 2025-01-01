#ifndef DATASET_LOADER_H
#define DATASET_LOADER_H

#include <cstddef>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <map>
#include "simdjson.h"

struct BenchmarkResult {
    std::string dataset_name;
    std::string compressor_name;
    double compression_rate;
    double compression_speed;
    double decompression_speed;
    double random_access_speed;
    double average_random_access_time;
};

struct Dataset {
    std::string dataset_name;
    std::vector<std::string> data;
    std::vector<size_t> queries;

    static Dataset load(const std::filesystem::path& path);
};

std::vector<Dataset> load_datasets(const std::filesystem::path& dir);
std::tuple<std::string, std::vector<uint8_t>, std::vector<size_t>, std::vector<size_t>> process_dataset(const Dataset& dataset);
void print_benchmark_results(const std::vector<BenchmarkResult>& results);

#endif // DATASET_LOADER_H