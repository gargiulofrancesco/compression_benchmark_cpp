#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <chrono>
#include <sched.h>
#include "copy_compressor.h"
#include "fsst_compressor.h"
#include "dataset_loader.h"

using namespace std;
using namespace std::chrono;
using json = nlohmann::json;

void append_result_to_file(const BenchmarkResult& result, const std::filesystem::path& output_file) {
    std::vector<BenchmarkResult> results;

    // Read existing results if the file exists
    if (std::filesystem::exists(output_file)) {
        std::ifstream input(output_file);
        if (input.is_open()) {
            json existing_results;
            input >> existing_results;
            results = existing_results.get<std::vector<BenchmarkResult>>();
            input.close();
        }
    }

    // Append the new result
    results.push_back(result);

    // Write back to the file
    std::ofstream output(output_file);
    if (!output.is_open()) {
        throw std::runtime_error("Failed to open output file.");
    }
    output << json(results).dump(4);
    output.close();
}

template <typename CompressorType>
BenchmarkResult benchmark(CompressorType& compressor,
                          const std::string& dataset_name,
                          const std::vector<uint8_t>& data,
                          const std::vector<size_t>& end_positions,
                          const std::vector<size_t>& queries) {
    // Force memory alignment
    alignas(64) std::vector<uint8_t> buffer(data.size() + 1024);
    uint64_t dummy = 0;

    // Calculate the size of data to access directly
    const double data_bytes = static_cast<double>(data.size());
    size_t random_access_bytes = 0;
    for (size_t i : queries) {
        size_t prev = (i == 0) ? 0 : end_positions[i - 1];
        random_access_bytes += end_positions[i] - prev;
    }

    // Initialize benchmark results
    BenchmarkResult result;
    result.dataset_name = dataset_name;
    result.compressor_name = compressor.name();

    // Compression
    auto start_compression = high_resolution_clock::now();
    {
        compressor.compress(data, end_positions);
    }
    auto end_compression = high_resolution_clock::now();
    std::atomic_thread_fence(std::memory_order_seq_cst);

    double compression_time = duration_cast<duration<double>>(end_compression - start_compression).count();
    result.compression_rate = data_bytes / static_cast<double>(compressor.space_used_bytes());
    result.compression_speed = (data_bytes / (1024.0 * 1024.0)) / compression_time;

    // Decompression
    auto start_decompression = high_resolution_clock::now();
    {
        compressor.decompress(buffer);
    }
    auto end_decompression = high_resolution_clock::now();
    std::atomic_thread_fence(std::memory_order_seq_cst);

    double decompression_time = duration_cast<duration<double>>(end_decompression - start_decompression).count();
    result.decompression_speed = (data_bytes / (1024.0 * 1024.0)) / decompression_time;

    // Prevent the compiler from optimizing
    for (const auto& b : buffer) {
        dummy ^= b;
    }

    // Random Access
    double total_random_access_time = 0.0;
    for (size_t query : queries) {
        size_t item_size = end_positions[query] - ((query == 0) ? 0 : end_positions[query - 1]);
        
        // Clear the buffer
        buffer.clear();
        buffer.shrink_to_fit();
        buffer.resize(item_size);
        
        auto start_random_access = high_resolution_clock::now();
        {
            compressor.get_item_at(query, buffer);
        }
        auto end_random_access = high_resolution_clock::now();
        std::atomic_thread_fence(std::memory_order_seq_cst);

        double random_access_time = duration_cast<duration<double>>(end_random_access - start_random_access).count();
        total_random_access_time += random_access_time;
        
        // Prevent the compiler from optimizing
        for (const auto& b : buffer) {
            dummy ^= b;
        }
    }

    result.random_access_speed = (static_cast<double>(random_access_bytes) / (1024.0 * 1024.0)) / total_random_access_time;
    result.average_random_access_time = total_random_access_time / queries.size();

    if (dummy == 0xDEADBEEF) {
        std::cout << "This will never print but prevents optimization" << std::endl;
    }

    return result;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <dataset_path> <compressor_name> <output_file>\n";
        return 1;
    }

    std::filesystem::path dataset_path(argv[1]);
    std::string compressor_name(argv[2]);
    std::filesystem::path output_file(argv[3]);

    // Validate dataset path
    if (!std::filesystem::exists(dataset_path)) {
        std::cerr << "Error: Dataset path '" << dataset_path << "' does not exist.\n";
        return 1;
    }
    if (!std::filesystem::is_regular_file(dataset_path)) {
        std::cerr << "Error: Dataset path '" << dataset_path << "' is not a file.\n";
        return 1;
    }

    try {
        // Load dataset
        auto dataset = Dataset::load(dataset_path);
        auto [dataset_name, data, end_positions, queries] = process_dataset(dataset);

        // Initialize the compressor
        BenchmarkResult result;
        if (compressor_name == "fsst") {
            FSSTCompressor compressor = FSSTCompressor::create(data.size(), end_positions.size());
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        } 
        else if (compressor_name == "copy") {
            CopyCompressor compressor = CopyCompressor::create(data.size(), end_positions.size());
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else {
            std::cerr << "Unknown compressor: " << compressor_name << "\n";
            return 1;
        }

        // Append the result to the output file
        append_result_to_file(result, output_file);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
