#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <random>
#include "fsst_compressor.h"
#include "dataset_loader.h"

struct BenchmarkResult {
    std::string dataset_name;
    std::string compressor_name;
    double compression_rate;
    double compression_speed;
    double decompression_speed;
    double random_access_speed;
    double average_random_access_time;
};

template <typename CompressorType>
BenchmarkResult benchmark(Compressor<CompressorType>& compressor, 
                          const std::string& dataset_name, 
                          const std::vector<uint8_t>& data, 
                          const std::vector<size_t>& end_positions, 
                          const std::vector<size_t>& queries) {
    using namespace std::chrono;

    const double data_bytes = static_cast<double>(data.size());
    size_t random_access_bytes = 0;

    for (size_t i: queries) {
        size_t prev = (i == 0) ? 0 : end_positions[i - 1];
        random_access_bytes += end_positions[i] - prev;
    }
    
    BenchmarkResult result;
    result.dataset_name = dataset_name;
    result.compressor_name = compressor.name();

    // Compression
    auto start_compression = high_resolution_clock::now();
    compressor.compress(data, end_positions);
    auto end_compression = high_resolution_clock::now();
    auto compression_time = duration_cast<duration<double>>(end_compression - start_compression).count();

    result.compression_rate = data_bytes / static_cast<double>(compressor.space_used_bytes());
    result.compression_speed = (data_bytes / (1024.0 * 1024.0)) / compression_time;

    // Decompression
    std::vector<uint8_t> buffer(data.size() + 1024);
    auto start_decompression = high_resolution_clock::now();
    compressor.decompress(buffer);
    auto end_decompression = high_resolution_clock::now();
    auto decompression_time = duration_cast<duration<double>>(end_decompression - start_decompression).count();

    result.decompression_speed = (data_bytes / (1024.0 * 1024.0)) / decompression_time;

    // Random Access
    double total_random_access_time = 0.0;
    for (size_t query : queries) {
        buffer.clear();
        auto start_random_access = high_resolution_clock::now();
        compressor.get_item_at(query, buffer);
        auto end_random_access = high_resolution_clock::now();
        auto random_access_time = duration_cast<duration<double>>(end_random_access - start_random_access).count();
        total_random_access_time += random_access_time;
    }

    result.random_access_speed = (static_cast<double>(random_access_bytes) / (1024.0 * 1024.0)) / total_random_access_time;
    result.average_random_access_time = total_random_access_time / queries.size();

    return result;
}

// Generate Queries Function
std::vector<size_t> generate_queries(size_t n_elements, double selectivity, uint64_t seed) {
    size_t n_queries = static_cast<size_t>(n_elements * selectivity);
    std::vector<size_t> queries(n_elements);
    std::iota(queries.begin(), queries.end(), 0);

    std::mt19937 rng(seed);
    std::shuffle(queries.begin(), queries.end(), rng);

    queries.resize(n_queries);
    std::sort(queries.begin(), queries.end());
    return queries;
}

void print_results(const std::vector<BenchmarkResult>& results) {
    // Group results by compressor name
    std::map<std::string, std::vector<BenchmarkResult>> grouped_results;
    for (const auto& result : results) {
        grouped_results[result.compressor_name].push_back(result);
    }

    // Process each compressor group
    for (const auto& [compressor, group] : grouped_results) {
        std::vector<BenchmarkResult> sorted_group = group;

        // Sort by dataset name
        std::sort(sorted_group.begin(), sorted_group.end(),
                  [](const BenchmarkResult& a, const BenchmarkResult& b) {
                      return a.dataset_name < b.dataset_name;
                  });

        // Print header for this compressor
        std::cout << "\nResults for Compressor: " << compressor << "\n";
        std::cout << std::left
                  << std::setw(20) << "Dataset"
                  << std::setw(15) << "Comp Rate"
                  << std::setw(20) << "Comp Speed (MB/s)"
                  << std::setw(20) << "Decomp Speed (MB/s)"
                  << std::setw(25) << "Rand Acc Speed (MB/s)"
                  << std::setw(20) << "Avg Rand Time (s)" << "\n";
        std::cout << std::string(120, '-') << "\n";

        // Print results for each dataset in the group
        for (const auto& result : sorted_group) {
            std::cout << std::left
                      << std::setw(20) << result.dataset_name
                      << std::setw(15) << std::fixed << std::setprecision(3) << result.compression_rate
                      << std::setw(20) << std::fixed << std::setprecision(2) << result.compression_speed
                      << std::setw(20) << std::fixed << std::setprecision(2) << result.decompression_speed
                      << std::setw(25) << std::fixed << std::setprecision(2) << result.random_access_speed
                      << std::setw(20) << std::fixed << std::setprecision(9) << result.average_random_access_time
                      << "\n";
        }

        // Calculate and print averages
        size_t n = sorted_group.size();
        double avg_compression_rate = std::accumulate(sorted_group.begin(), sorted_group.end(), 0.0,
                                                      [](double sum, const BenchmarkResult& r) {
                                                          return sum + r.compression_rate;
                                                      }) / n;
        double avg_compression_speed = std::accumulate(sorted_group.begin(), sorted_group.end(), 0.0,
                                                       [](double sum, const BenchmarkResult& r) {
                                                           return sum + r.compression_speed;
                                                       }) / n;
        double avg_decompression_speed = std::accumulate(sorted_group.begin(), sorted_group.end(), 0.0,
                                                         [](double sum, const BenchmarkResult& r) {
                                                             return sum + r.decompression_speed;
                                                         }) / n;
        double avg_random_access_speed = std::accumulate(sorted_group.begin(), sorted_group.end(), 0.0,
                                                         [](double sum, const BenchmarkResult& r) {
                                                             return sum + r.random_access_speed;
                                                         }) / n;
        double avg_average_random_access_time = std::accumulate(sorted_group.begin(), sorted_group.end(), 0.0,
                                                                [](double sum, const BenchmarkResult& r) {
                                                                    return sum + r.average_random_access_time;
                                                                }) / n;

        std::cout << std::string(120, '-') << "\n";
        std::cout << std::left
                  << std::setw(20) << "AVERAGE"
                  << std::setw(15) << std::fixed << std::setprecision(3) << avg_compression_rate
                  << std::setw(20) << std::fixed << std::setprecision(2) << avg_compression_speed
                  << std::setw(20) << std::fixed << std::setprecision(2) << avg_decompression_speed
                  << std::setw(25) << std::fixed << std::setprecision(2) << avg_random_access_speed
                  << std::setw(20) << std::fixed << std::setprecision(9) << avg_average_random_access_time
                  << "\n";
    }
}

/*
int main() {
    // Example input strings
    std::vector<std::string> inputStrings = {"string1", "string2", "string3", "longer_string"};
    
    // Prepare data for compression (convert strings to uint8_t and track end positions)
    std::vector<uint8_t> inputData;
    std::vector<size_t> end_positions;
    size_t total_size = 0;

    for (const auto& str : inputStrings) {
        total_size += str.size();
        inputData.insert(inputData.end(), str.begin(), str.end());
        end_positions.push_back(total_size);  // Track the end position of each string
    }

    // Create FSSTCompressor instance using the Compressor template
    FSSTCompressor fsst = FSSTCompressor::create(total_size, end_positions.size());

    // Perform compression
    fsst.compress(inputData, end_positions);
    std::cout << "Compression complete." << std::endl;

    // Perform decompression
    std::vector<uint8_t> decompressedData(total_size);
    fsst.decompress(decompressedData);
    std::cout << "Decompression complete." << std::endl;

    // Output the decompressed data (convert it back to strings for display)
    // For simplicity, assume each item is smaller than 1024 bytes in decompressed form
    std::vector<std::string> decompressedStrings(inputStrings.size());
    size_t start = 0;
    for (size_t i = 0; i < inputStrings.size(); ++i) {
        size_t end = end_positions[i];
        std::string item(decompressedData.begin() + start, decompressedData.begin() + end);
        decompressedStrings[i] = item;
        start = end;
    }

    // Print out the decompressed strings
    for (const auto& str : decompressedStrings) {
        std::cout << "Decompressed string: " << str << std::endl;
    }

    // Retrieve a specific item at index 2 (for example)
    std::vector<uint8_t> itemBuffer(1024);
    fsst.get_item_at(2, itemBuffer); // Get item at index 2
    std::cout << "Item at index 2: ";
    for (uint8_t byte : itemBuffer) {
        std::cout << byte;
    }
    std::cout << std::endl;

    return 0;
}
*/


int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Error: Missing directory argument. Usage is: " << argv[0] << " <directory>\n";
        return 1;
    }

    std::filesystem::path directory(argv[1]);

    // Check if the path is a valid directory
    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        std::cerr << "Error: " << directory << " is not a valid directory.\n";
        return 1;
    }

    double selectivity = 0.15;
    uint64_t seed = 42;
    std::vector<BenchmarkResult> results;

    try {
        // Load the dataset from the JSON files in the directory
        std::vector<Dataset> datasets = load_datasets(directory);
        
        // Process each dataset
        for (const auto& dataset: datasets) {
            auto [name, data, end_positions] = process_dataset(dataset);
            auto queries = generate_queries(end_positions.size(), selectivity, seed);

            std::cout << "Benchmarking dataset: " << name << "\n";

            // Add new compressor types here
            FSSTCompressor fsst = FSSTCompressor::create(data.size(), end_positions.size());
            BenchmarkResult result = benchmark(fsst, name, data, end_positions, queries);
            results.push_back(result);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    print_results(results);

    return 0;
}
