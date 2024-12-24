#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
#include "copy_compressor.h"
#include "fsst_compressor.h"
#include "dataset_loader.h"

template <typename CompressorType>
void test(Compressor<CompressorType>& compressor, 
          const std::string& dataset_name, 
          const std::vector<uint8_t>& data, 
          const std::vector<size_t>& end_positions) {

    // Compression and Decompression Test
    std::vector<uint8_t> buffer(data.size() + 1024);
    compressor.compress(data, end_positions);
    compressor.decompress(buffer);
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] != buffer[i]) {
            std::cerr << "Error: Decompression failed at index " << i << " for compressor " << compressor.name() << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }

    // Random Access Test
    for(size_t i=0; i<end_positions.size(); i++) {
        size_t start = (i == 0) ? 0 : end_positions[i - 1];
        size_t end = end_positions[i];
        size_t item_size = end - start;
        buffer.resize(item_size);
        compressor.get_item_at(i, buffer);        
        for(size_t j=0; j<end-start; j++) {
            if(data[start + j] != buffer[j]) {
                std::cerr << "Error: Random Access failed at index " << i << " for compressor " << compressor.name() << std::endl;
                std::exit(EXIT_FAILURE);
            }
        }
    }
}

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

    try {
        // Load the dataset from the JSON files in the directory
        std::vector<Dataset> datasets = load_datasets(directory);
        
        // Process each dataset
        for (const auto& dataset: datasets) {
            auto [name, data, end_positions, queries] = process_dataset(dataset);
            std::cout << "Testing dataset: " << name << "\n";

            // Add new compressor types here
            CopyCompressor copy = CopyCompressor::create(data.size(), end_positions.size());
            test(copy, name, data, end_positions);

            FSSTCompressor fsst = FSSTCompressor::create(data.size(), end_positions.size());
            test(fsst, name, data, end_positions);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
