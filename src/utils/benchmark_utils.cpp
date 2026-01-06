#include "benchmark_utils.h"
#include "onpair_compressor.h"
#include <fstream>
#include <map>
#include <stdexcept>
#include "simdjson.h"
#include <random>
#ifdef __linux__
#include <sched.h>
#include <system_error>
#endif

std::pair<std::vector<uint8_t>, std::vector<size_t>> load_dataset(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }

    // Parse the JSON file
    simdjson::ondemand::parser parser;
    simdjson::padded_string json = simdjson::padded_string::load(path.string());

    auto doc = parser.iterate(json);
    std::vector<std::string> strings;
    for(auto str : doc){
        strings.push_back(std::string(str.get_string().value()));
    }

    // Process the strings
    std::vector<uint8_t> data;
    std::vector<size_t> end_positions;

    // Start with 0, then append cumulative string lengths for boundary indexing
    end_positions.push_back(0);
    for (const auto& str : strings) {
        data.insert(data.end(), str.begin(), str.end());
        end_positions.push_back(end_positions.back() + str.size());
    }

    return {data, end_positions};
}

std::vector<size_t> generate_random_queries(size_t n, size_t n_queries){
    std::vector<size_t> queries;
    queries.reserve(n_queries);
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, n - 1);
    for (size_t i = 0; i < n_queries; ++i) {
        queries.push_back(dist(rng));
    }

    return queries;
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
                br.average_random_access_time = result["average_random_access_time"].get_uint64().value();
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
                br.average_random_access_time = res["average_random_access_time"].get_uint64().value();
                results.push_back(br);
            }
        } catch (const simdjson::simdjson_error& e) {
            auto msg = std::string("Error parsing results file: ") + e.what();
            throw std::runtime_error(msg);
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
        double avg_compression_rate = 0, avg_compression_speed = 0, avg_decompression_speed = 0, avg_random_access_time = 0;

        for (const auto& result : group) {
            avg_compression_rate += result.compression_rate;
            avg_compression_speed += result.compression_speed;
            avg_decompression_speed += result.decompression_speed;
            avg_random_access_time += result.average_random_access_time;
        }

        BenchmarkResult averaged_result = {
            dataset,
            compressor,
            avg_compression_rate / len,
            avg_compression_speed / len,
            avg_decompression_speed / len,
            static_cast<size_t>(avg_random_access_time / len)
        };
        compressor_groups[compressor].push_back(averaged_result);
    }

    // Calculate overall averages for each compressor
    for (const auto& [compressor, results] : compressor_groups) {
        size_t len = results.size();
        double overall_avg_compression_rate = 0, overall_avg_compression_speed = 0, overall_avg_decompression_speed = 0, overall_avg_random_access_time = 0;

        for (const auto& result : results) {
            overall_avg_compression_rate += result.compression_rate;
            overall_avg_compression_speed += result.compression_speed;
            overall_avg_decompression_speed += result.decompression_speed;
            overall_avg_random_access_time += result.average_random_access_time;
        }

        compressor_averages[compressor] = {
            "AVERAGE",
            compressor,
            overall_avg_compression_rate / len,
            overall_avg_compression_speed / len,
            overall_avg_decompression_speed / len,
            static_cast<size_t>(overall_avg_random_access_time / len)
        };
    }

    // Print grouped and averaged results
    for (const auto& [compressor, results] : compressor_groups) {
        std::cout << "\nResults for Compressor: " << compressor << "\n";
        std::cout << std::setw(20) << "Dataset"
                  << std::setw(16) << "Comp. Rate"
                  << std::setw(24) << "Comp. Speed (MiB/s)"
                  << std::setw(24) << "Decomp. Speed (MiB/s)"
                  << std::setw(24) << "Avg. Access Time (ns)" << "\n";

        for (const auto& result : results) {
            std::cout << std::setw(20) << result.dataset_name
                      << std::setw(16) << std::fixed << std::setprecision(3) << result.compression_rate
                      << std::setw(24) << std::fixed << std::setprecision(2) << result.compression_speed
                      << std::setw(24) << std::fixed << std::setprecision(2) << result.decompression_speed
                      << std::setw(24) << std::fixed << std::setprecision(0) << result.average_random_access_time << "\n";
        }

        // Print the overall averages row
        const auto& avg_result = compressor_averages[compressor];
        std::cout << std::setw(20) << avg_result.dataset_name
                  << std::setw(16) << std::fixed << std::setprecision(3) << avg_result.compression_rate
                  << std::setw(24) << std::fixed << std::setprecision(2) << avg_result.compression_speed
                  << std::setw(24) << std::fixed << std::setprecision(2) << avg_result.decompression_speed
                  << std::setw(24) << std::fixed << std::setprecision(0) << avg_result.average_random_access_time << "\n";
    }
}

std::pair<size_t, size_t> find_onpair_params(
    const std::vector<uint8_t>& data,
    const std::vector<size_t>& end_positions,
    const size_t target_sample_size,
    const size_t seed
) {
    size_t n_elements = end_positions.size() - 1;
    size_t data_size = end_positions.back();

    size_t threshold = 2;
    size_t prev_sample_size = 0;
    while (true) {
        OnPairCompressor onpair(data_size, n_elements);
        onpair.set_seed(seed);
        onpair.set_threshold(threshold);
        auto permutation = onpair.generate_random_permutation(n_elements);
        auto [_, sample_size] = onpair.train_dictionary(data.data(), end_positions, permutation);

        if (sample_size >= target_sample_size) {
            if(threshold == 2) {
                return {threshold, sample_size};
            }
            size_t diff_curr = sample_size - target_sample_size;
            size_t diff_prev = target_sample_size - prev_sample_size;
            if (diff_prev < diff_curr) {
                return {threshold - 1, prev_sample_size};
            } else {
                return {threshold, sample_size};
            }
        }

        prev_sample_size = sample_size;
        threshold++;
    }
}

#ifdef __linux__
bool try_set_affinity(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    return sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == 0;
}
#else
bool try_set_affinity(int core_id) {
    // CPU affinity is not supported on this platform
    (void)core_id; // Suppress unused parameter warning
    return false;
}
#endif