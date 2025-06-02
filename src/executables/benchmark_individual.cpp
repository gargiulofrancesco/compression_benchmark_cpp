#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <chrono>
#include <sched.h>
#include <stdexcept>
#include "raw_compressor.h"
#include "fsst_compressor.h"
#include "fsst_block_compressor.h"
#include "lz4_compressor.h"
#include "snappy_compressor.h"
#include "xz_compressor.h"
#include "brotli_compressor.h"
#include "deflate_compressor.h"
#include "zstd_compressor.h"
#include "onpair16_compressor.h"
#include "onpair_compressor.h"
#include "deflate_individual_compressor.h"
#include "bpe_compressor.h"
#include "bpe_lpm_compressor.h"
#include "benchmark_utils.h"

const int DEFAULT_CORE_ID = 0;
const size_t N_QUERIES = 1000000;

template <typename CompressorType>
BenchmarkResult benchmark(CompressorType& compressor,
                          const std::string& dataset_name,
                          const std::vector<uint8_t>& data,
                          const std::vector<size_t>& end_positions,
                          const std::vector<size_t>& queries) {
    std::vector<uint8_t> buffer(data.size() + 1024);

    // Calculate the total size of random access data
    const double data_bytes = static_cast<double>(data.size());

    // Initialize benchmark result
    BenchmarkResult result;
    result.dataset_name = dataset_name;
    result.compressor_name = compressor.name();

    // Compression
    auto start_compression = std::chrono::high_resolution_clock::now();
    compressor.compress(data.data(), end_positions);
    auto end_compression = std::chrono::high_resolution_clock::now();

    double compression_time = std::chrono::duration_cast<std::chrono::duration<double>>(end_compression - start_compression).count();
    result.compression_rate = data_bytes / static_cast<double>(compressor.space_used_bytes());
    result.compression_speed = (data_bytes / (1024.0 * 1024.0)) / compression_time;

    // Decompression
    auto start_decompression = std::chrono::high_resolution_clock::now();
    size_t b_size = compressor.decompress(buffer.data());
    auto end_decompression = std::chrono::high_resolution_clock::now();
    
    if (!std::equal(data.data(), data.data() + data.size(), buffer.data())) {
        throw std::runtime_error("Data mismatch during decompression for compressor: " + std::string(compressor.name()));
    }
    double decompression_time = std::chrono::duration_cast<std::chrono::duration<double>>(end_decompression - start_decompression).count();
    result.decompression_speed = (data_bytes / (1024.0 * 1024.0)) / decompression_time;

    // Random Access
    size_t total_random_access_time = 0;
    for (size_t query : queries) {
        size_t start_position = end_positions[query];
        size_t end_position = end_positions[query+1];
        size_t item_size = end_position - start_position;
        
        auto start_random_access = std::chrono::high_resolution_clock::now();
        size_t b_size = compressor.get_item_at(query, buffer.data());
        auto end_random_access = std::chrono::high_resolution_clock::now();
        
        if (!std::equal(data.data() + start_position, data.data() + end_position, buffer.data())) {
            throw std::runtime_error("Data mismatch during random access for compressor: " + std::string(compressor.name()));
        }
        size_t random_access_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end_random_access - start_random_access).count();
        total_random_access_time += random_access_time;
    }

    result.average_random_access_time = total_random_access_time / queries.size();

    return result;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <dataset_path> <compressor_name> <output_file> [core_id]\n";
        return 1;
    }

    std::filesystem::path dataset_path(argv[1]);
    std::string compressor_name(argv[2]);
    std::filesystem::path output_file(argv[3]);
    int core_id = (argc > 4) ? std::stoi(argv[4]) : DEFAULT_CORE_ID;

    // Validate dataset path
    if (!std::filesystem::exists(dataset_path)) {
        std::cerr << "Error: Dataset path '" << dataset_path << "' does not exist.\n";
        return 1;
    }
    if (!std::filesystem::is_regular_file(dataset_path)) {
        std::cerr << "Error: Dataset path '" << dataset_path << "' is not a file.\n";
        return 1;
    }

    // Set CPU affinity
    set_affinity(core_id);

    try {
        // Load dataset
        std::string dataset_name = dataset_path.filename().string();
        auto [data, end_positions] = load_dataset(dataset_path);
        size_t n_elements = end_positions.size() - 1;
        std::vector<size_t> queries = generate_random_queries(n_elements, N_QUERIES);

        // Initialize the compressor
        BenchmarkResult result;
        if (compressor_name == "raw") {
            RawCompressor compressor = RawCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "lz4") {
            LZ4Compressor compressor = LZ4Compressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "snappy") {
            SnappyCompressor compressor = SnappyCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "xz") {
            XZCompressor compressor = XZCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "zstd") {
            ZstdCompressor compressor = ZstdCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "deflate") {
            DeflateCompressor compressor = DeflateCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "brotli") {
            BrotliCompressor compressor = BrotliCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "fsst") {
            FSSTCompressor compressor = FSSTCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        } 
        else if (compressor_name == "fsst_block") {
            FSSTBlockCompressor compressor = FSSTBlockCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "onpair16") {
            OnPair16Compressor compressor = OnPair16Compressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "onpair") {
            OnPairCompressor compressor = OnPairCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "deflate_individual") {
            DeflateIndividualCompressor compressor = DeflateIndividualCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "bpe") {
            BPECompressor compressor = BPECompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else if (compressor_name == "bpe_lpm") {
            BPELPMCompressor compressor = BPELPMCompressor::create(data.size(), end_positions.size()-1);
            result = benchmark(compressor, dataset_name, data, end_positions, queries);
        }
        else {
            std::cerr << "Unknown compressor: " << compressor_name << "\n";
            return 1;
        }

        // Append the result to the output file
        append_benchmark_result(result, output_file);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
