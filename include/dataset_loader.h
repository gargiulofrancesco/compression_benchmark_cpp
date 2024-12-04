#ifndef DATASET_LOADER_H
#define DATASET_LOADER_H

#include <cstddef>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include "json.hpp"

struct BenchmarkResult {
    std::string dataset_name;
    std::string compressor_name;
    double compression_rate;
    double compression_speed;
    double decompression_speed;
    double random_access_speed;
    double average_random_access_time;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(BenchmarkResult, dataset_name, compressor_name, compression_rate, compression_speed, decompression_speed, random_access_speed, average_random_access_time)
};

struct Dataset {
    std::string dataset_name;
    std::vector<std::string> data;
    std::vector<size_t> queries;

    static Dataset load(const std::filesystem::path& path);
};

std::vector<Dataset> load_datasets(const std::filesystem::path& dir);
std::tuple<std::string, std::vector<uint8_t>, std::vector<size_t>, std::vector<size_t>> process_dataset(const Dataset& dataset);

#endif // DATASET_LOADER_H