#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
#include "copy_compressor.h"
#include "fsst_compressor.h"
#include "onpair16_compressor.h"
#include "dataset_loader.h"
#include "memory_buffer.h"

template <typename CompressorType>
void test(Compressor<CompressorType>& compressor, 
          const std::string& dataset_name, 
          const std::vector<uint8_t>& data, 
          const std::vector<size_t>& end_positions) {

    MemoryBuffer buffer(data.size() + 1024);

    // Compression and Decompression Test
    compressor.compress(data.data(), end_positions);
    size_t b_size = compressor.decompress(buffer.data());
    buffer.set_size(b_size);
    for (size_t i = 0; i < data.size(); ++i) {
        if (i >= buffer.size() || data[i] != buffer[i]) {
            std::cerr << "Error: Decompression failed at index " << i << " for compressor " << compressor.name() << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }

    // Random Access Test
    for(size_t i=0; i<end_positions.size(); i++) {
        size_t start = (i == 0) ? 0 : end_positions[i - 1];
        size_t end = end_positions[i];
        size_t item_size = end - start;

        buffer.clear();
        size_t b_size = compressor.get_item_at(i, buffer.data());  
        buffer.set_size(b_size);

        for(size_t j=0; j<item_size; j++) {
            if(j >= buffer.size() || data[start + j] != buffer[j]) {
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
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                try {
                    // Load and process one dataset at a time
                    Dataset dataset = Dataset::load(entry.path());

                    auto [name, data, end_positions, queries] = process_dataset(dataset);
                    std::cout << "Testing dataset: " << name << "\n";
                    
                    // Add new compressor types here
                    CopyCompressor copy = CopyCompressor::create(data.size(), end_positions.size());
                    test(copy, name, data, end_positions);

                    FSSTCompressor fsst = FSSTCompressor::create(data.size(), end_positions.size());
                    test(fsst, name, data, end_positions);

                    OnPair16Compressor onpair16 = OnPair16Compressor::create(data.size(), end_positions.size());
                    test(onpair16, name, data, end_positions);

                } catch (const std::exception& e) {
                    std::cerr << "Error processing file " << entry.path() << ": " << e.what() << "\n";
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}