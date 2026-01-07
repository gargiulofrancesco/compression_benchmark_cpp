#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <numeric>
#include <algorithm>
#include "onpair_compressor.h"
#include "benchmark_utils.h"

// Fixed seed and target sample fraction for OnPair's dictionary training
const size_t SAMPLING_SEED = 42;
const float TARGET_SAMPLE_FRACTION = 0.1f;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <dataset_path>" << std::endl;
        return 1;
    }

    // Load dataset
    std::string dataset_path = argv[1];
    std::string dataset_name = std::filesystem::path(dataset_path).filename().string();
    std::vector<uint8_t> data;
    std::vector<size_t> end_positions;

    try {
        auto result = load_dataset(dataset_path);
        data = std::move(result.first);
        end_positions = std::move(result.second);
    } catch (const std::exception& e) {
        std::cerr << "Error loading dataset: " << e.what() << std::endl;
        return 1;
    }

    size_t n_elements = end_positions.size() - 1;
    size_t data_size = data.size();
    
    // Compress data using OnPair
    size_t target_sample_size = static_cast<size_t>(data_size * TARGET_SAMPLE_FRACTION);
    auto [threshold, _] = find_onpair_params(data, end_positions, target_sample_size, SAMPLING_SEED);
    OnPairCompressor compressor = OnPairCompressor::create(data_size, n_elements);
    compressor.set_seed(SAMPLING_SEED);
    compressor.set_threshold(threshold);
    compressor.compress(data.data(), end_positions);

    size_t compressed_size = compressor.space_used_bytes();
    double compression_ratio = static_cast<double>(data_size) / compressed_size;

    // Compute statistics
    auto stats = compressor.get_token_statistics();

    size_t total_frequency = 0;
    size_t total_token_len = 0;
    size_t dict_data_size = 0;
    size_t dict_offsets_size = (stats.size() + 1) * sizeof(uint32_t);
    size_t min_token_len = std::numeric_limits<size_t>::max();
    size_t max_token_len = 0;

    for (const auto& s : stats) {
        total_frequency += s.frequency;
        total_token_len += s.length * s.frequency;
        dict_data_size += s.length;
        if (s.length < min_token_len) min_token_len = s.length;
        if (s.length > max_token_len) max_token_len = s.length;
    }

    double avg_token_len = static_cast<double>(total_token_len) / total_frequency;

    std::cout << "Dataset: " << dataset_name << std::endl;
    std::cout << "Initial size: " << data_size << " bytes" << std::endl;
    std::cout << "Compressed size: " << compressed_size << " bytes" << std::endl;
    std::cout << "Compression ratio: " << compression_ratio << std::endl;
    std::cout << "--------------------------------" << std::endl;
    std::cout << "Dictionary Tokens: " << stats.size() << std::endl;
    std::cout << "Min. token length: " << min_token_len << std::endl;
    std::cout << "Max. token length: " << max_token_len << std::endl;
    std::cout << "Avg. token length: " << avg_token_len << std::endl;
    std::cout << "Dictionary data size: " << dict_data_size << " bytes" << std::endl;
    std::cout << "Dictionary total size (with offsets): " << dict_data_size + dict_offsets_size << " bytes" << std::endl;

    return 0;
}
