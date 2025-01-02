#include "benchmark_utils.h"
#include <fstream>
#include <map>
#include <stdexcept>
#include "simdjson.h"

Dataset load_dataset(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    simdjson::ondemand::parser parser;
    simdjson::padded_string json = simdjson::padded_string::load(path.string());
    
    auto doc = parser.iterate(json);
    Dataset dataset;
    
    // Get dataset name
    dataset.dataset_name = std::string(doc["dataset_name"].get_string().value());
    
    // Load data array
    dataset.data.clear();
    for (auto value : doc["data"]) {
        dataset.data.push_back(std::string(value.get_string().value()));
    }
    
    // Load queries array
    dataset.queries.clear();
    for (auto value : doc["queries"]) {
        dataset.queries.push_back(value.get_uint64().value());
    }

    return dataset;
}

std::vector<Dataset> load_datasets(const std::filesystem::path& dir) {
    std::vector<Dataset> datasets;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            datasets.push_back(load_dataset(entry.path()));
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

std::vector<BenchmarkResult> read_benchmark_results(const std::filesystem::path& file_path) {
    std::vector<BenchmarkResult> results;

    if (std::filesystem::exists(file_path)) {
        try {
            simdjson::ondemand::parser parser;
            simdjson::padded_string json = simdjson::padded_string::load(file_path.string());
            simdjson::ondemand::document doc = parser.iterate(json);
            
            for (simdjson::ondemand::object result : doc.get_array()) {
                BenchmarkResult br;
                br.dataset_name = std::string(result["dataset_name"].get_string().value());
                br.compressor_name = std::string(result["compressor_name"].get_string().value());
                br.compression_rate = result["compression_rate"].get_double().value();
                br.compression_speed = result["compression_speed"].get_double().value();
                br.decompression_speed = result["decompression_speed"].get_double().value();
                br.random_access_speed = result["random_access_speed"].get_double().value();
                br.average_random_access_time = result["average_random_access_time"].get_double().value();
                results.push_back(br);
            }
        } catch (const simdjson::simdjson_error& e) {
            throw std::runtime_error("Error parsing results file '" + file_path.string() + ": " + e.what());
        }
    }
    return results;
}

void append_benchmark_result(const BenchmarkResult& result, const std::filesystem::path& output_file) {
    std::vector<BenchmarkResult> results;

    // Read existing results if the file exists
    if (std::filesystem::exists(output_file)) {
        try {
            simdjson::ondemand::parser parser;
            simdjson::padded_string json = simdjson::padded_string::load(output_file.string());
            simdjson::ondemand::document doc = parser.iterate(json);
            
            // Parse existing results
            for (simdjson::ondemand::object res : doc.get_array()) {
                BenchmarkResult br;
                br.dataset_name = std::string(res["dataset_name"].get_string().value());
                br.compressor_name = std::string(res["compressor_name"].get_string().value());
                br.compression_rate = res["compression_rate"].get_double().value();
                br.compression_speed = res["compression_speed"].get_double().value();
                br.decompression_speed = res["decompression_speed"].get_double().value();
                br.random_access_speed = res["random_access_speed"].get_double().value();
                br.average_random_access_time = res["average_random_access_time"].get_double().value();
                results.push_back(br);
            }
        } catch (const simdjson::simdjson_error& e) {
            std::cerr << "Warning: Could not parse existing results: " << e.what() << std::endl;
        }
    }

    // Append the new result
    results.push_back(result);

    // Write back to the file
    std::ofstream output(output_file);
    if (!output.is_open()) {
        throw std::runtime_error("Failed to open output file.");
    }

    output << "[\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        output << "    {\n";
        output << "        \"dataset_name\": \"" << r.dataset_name << "\",\n";
        output << "        \"compressor_name\": \"" << r.compressor_name << "\",\n";
        output << "        \"compression_rate\": " << r.compression_rate << ",\n";
        output << "        \"compression_speed\": " << r.compression_speed << ",\n";
        output << "        \"decompression_speed\": " << r.decompression_speed << ",\n";
        output << "        \"random_access_speed\": " << r.random_access_speed << ",\n";
        output << "        \"average_random_access_time\": " << r.average_random_access_time;
        output << "\n    }" << (i < results.size() - 1 ? "," : "") << "\n";
    }
    output << "]\n";
    output.close();
}

void print_benchmark_results(const std::vector<BenchmarkResult>& results) {
    // Group results by (compressor, dataset) pair
    std::map<std::pair<std::string, std::string>, std::vector<BenchmarkResult>> grouped_results;
    for (const auto& result : results) {
        grouped_results[{result.compressor_name, result.dataset_name}].push_back(result);
    }

    // Calculate averages and group by compressor
    std::map<std::string, std::vector<BenchmarkResult>> compressor_groups;
    std::map<std::string, BenchmarkResult> compressor_averages;

    for (const auto& [key, group] : grouped_results) {
        const auto& [compressor, dataset] = key;
        size_t len = group.size();
        double avg_compression_rate = 0, avg_compression_speed = 0, avg_decompression_speed = 0,
               avg_random_access_speed = 0, avg_average_random_access_time = 0;

        for (const auto& result : group) {
            avg_compression_rate += result.compression_rate;
            avg_compression_speed += result.compression_speed;
            avg_decompression_speed += result.decompression_speed;
            avg_random_access_speed += result.random_access_speed;
            avg_average_random_access_time += result.average_random_access_time;
        }

        BenchmarkResult averaged_result = {
            dataset,
            compressor,
            avg_compression_rate / len,
            avg_compression_speed / len,
            avg_decompression_speed / len,
            avg_random_access_speed / len,
            avg_average_random_access_time / len
        };
        compressor_groups[compressor].push_back(averaged_result);
    }

    // Calculate overall averages for each compressor
    for (const auto& [compressor, results] : compressor_groups) {
        size_t len = results.size();
        double overall_avg_compression_rate = 0, overall_avg_compression_speed = 0, 
               overall_avg_decompression_speed = 0, overall_avg_random_access_speed = 0, 
               overall_avg_random_access_time = 0;

        for (const auto& result : results) {
            overall_avg_compression_rate += result.compression_rate;
            overall_avg_compression_speed += result.compression_speed;
            overall_avg_decompression_speed += result.decompression_speed;
            overall_avg_random_access_speed += result.random_access_speed;
            overall_avg_random_access_time += result.average_random_access_time;
        }

        compressor_averages[compressor] = {
            "AVERAGE",
            compressor,
            overall_avg_compression_rate / len,
            overall_avg_compression_speed / len,
            overall_avg_decompression_speed / len,
            overall_avg_random_access_speed / len,
            overall_avg_random_access_time / len
        };
    }

    // Print grouped and averaged results
    for (const auto& [compressor, results] : compressor_groups) {
        std::cout << "\nResults for Compressor: " << compressor << "\n";
        std::cout << std::setw(20) << "Dataset"
                  << std::setw(12) << "C. Rate"
                  << std::setw(20) << "C. Speed (MB/s)"
                  << std::setw(20) << "D. Speed (MB/s)"
                  << std::setw(20) << "R.A. Speed (MB/s)"
                  << std::setw(20) << "R.A. Time (ns)" << "\n";

        for (const auto& result : results) {
            std::cout << std::setw(20) << result.dataset_name
                      << std::setw(12) << std::fixed << std::setprecision(3) << result.compression_rate
                      << std::setw(20) << std::fixed << std::setprecision(2) << result.compression_speed
                      << std::setw(20) << std::fixed << std::setprecision(2) << result.decompression_speed
                      << std::setw(20) << std::fixed << std::setprecision(2) << result.random_access_speed
                      << std::setw(20) << std::fixed << std::setprecision(0) << (result.average_random_access_time * 1e9) << "\n";
        }

        // Print the overall averages row
        const auto& avg_result = compressor_averages[compressor];
        std::cout << std::setw(20) << avg_result.dataset_name
                  << std::setw(12) << std::fixed << std::setprecision(3) << avg_result.compression_rate
                  << std::setw(20) << std::fixed << std::setprecision(2) << avg_result.compression_speed
                  << std::setw(20) << std::fixed << std::setprecision(2) << avg_result.decompression_speed
                  << std::setw(20) << std::fixed << std::setprecision(2) << avg_result.random_access_speed
                  << std::setw(20) << std::fixed << std::setprecision(0) << (avg_result.average_random_access_time * 1e9) << "\n";
    }
}