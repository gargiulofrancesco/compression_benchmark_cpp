#include <iostream>
#include <vector>
#include <string>
#include "fsst_compressor.h"  // Assuming this file includes Compressor<Derived> and FSSTCompressor

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
    std::vector<uint8_t> decompressedData;
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
    std::vector<uint8_t> itemBuffer;
    fsst.get_item_at(2, itemBuffer); // Get item at index 2
    std::cout << "Item at index 2: ";
    for (uint8_t byte : itemBuffer) {
        std::cout << byte;
    }
    std::cout << std::endl;

    return 0;
}
