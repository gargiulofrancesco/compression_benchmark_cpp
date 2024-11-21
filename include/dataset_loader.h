#ifndef DATASET_LOADER_H
#define DATASET_LOADER_H

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include "json.hpp"

struct Dataset {
    std::string dataset_name;
    std::vector<std::string> data;

    static Dataset load(const std::filesystem::path& path);
};

std::vector<Dataset> load_datasets(const std::filesystem::path& dir);
std::tuple<std::string, std::vector<uint8_t>, std::vector<size_t>> process_dataset(const Dataset& dataset);

#endif // DATASET_LOADER_H