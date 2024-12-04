#include "dataset_loader.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

Dataset Dataset::load(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    json j;
    file >> j;

    Dataset dataset;
    dataset.dataset_name = j.at("dataset_name").get<std::string>();
    dataset.data = j.at("data").get<std::vector<std::string>>();
    dataset.queries = j.at("queries").get<std::vector<size_t>>();

    return dataset;
}

std::vector<Dataset> load_datasets(const fs::path& dir) {
    std::vector<Dataset> datasets;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            datasets.push_back(Dataset::load(entry.path()));
        }
    }

    return datasets;
}

std::tuple<std::string, std::vector<uint8_t>, std::vector<size_t>, std::vector<size_t>> process_dataset(const Dataset& dataset) {
    const std::string& dataset_name = dataset.dataset_name;

    std::vector<uint8_t> data;
    std::vector<size_t> end_positions;

    size_t current_position = 0;
    for (const auto& s : dataset.data) {
        data.insert(data.end(), s.begin(), s.end());
        current_position += s.size();
        end_positions.push_back(current_position);
    }

    return {dataset_name, data, end_positions, dataset.queries};
}
