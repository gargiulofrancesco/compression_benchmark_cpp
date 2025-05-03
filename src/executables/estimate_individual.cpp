#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <chrono>
#include <sched.h>
#include <stdexcept>
#include "benchmark_utils.h"
#include <cassert>
#include <lz4.h>
#include <lz4hc.h>
#include <zstd.h>
#include <brotli/encode.h>
#include <brotli/decode.h>
#include <snappy.h>
#include <zlib.h>
#include <lzma.h>

const int DEFAULT_CORE_ID = 0;

BenchmarkResult compress_deflate(const std::string& dataset_name, const std::vector<uint8_t>& data, const int compression_level) {
    const double data_size_mb = data.size() / (1024.0 * 1024.0);

    BenchmarkResult result;
    result.dataset_name = dataset_name;
    result.compressor_name = "deflate -" + std::to_string(compression_level);
    result.random_access_speed = 0.0;
    result.average_random_access_time = 0.0;

    // Prepare compression buffer
    std::vector<uint8_t> compressed_buffer(data.size() * 2);
    
    // Compress
    z_stream strm{};
    deflateInit(&strm, compression_level);
    
    strm.next_in = const_cast<uint8_t*>(data.data());
    strm.avail_in = data.size();
    strm.next_out = compressed_buffer.data();
    strm.avail_out = compressed_buffer.size();
    
    auto start = std::chrono::high_resolution_clock::now();
    if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
        deflateEnd(&strm);
        throw std::runtime_error("Deflate compression failed");
    }
    
    size_t compressed_size = compressed_buffer.size() - strm.avail_out;
    compressed_buffer.resize(compressed_size);
    deflateEnd(&strm);
    
    auto compression_duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count();
    result.compression_speed = data_size_mb / compression_duration;
    result.compression_rate = static_cast<double>(data.size()) / static_cast<double>(compressed_size);
    
    // Decompress
    std::vector<uint8_t> decompressed_buffer(data.size());
    inflateInit(&strm);
    
    strm.next_in = compressed_buffer.data();
    strm.avail_in = compressed_size;
    strm.next_out = decompressed_buffer.data();
    strm.avail_out = decompressed_buffer.size();
    
    start = std::chrono::high_resolution_clock::now();
    if (inflate(&strm, Z_FINISH) != Z_STREAM_END) {
        inflateEnd(&strm);
        throw std::runtime_error("Deflate decompression failed");
    }
    
    inflateEnd(&strm);
    
    auto decompression_duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count();
    result.decompression_speed = data_size_mb / decompression_duration;
    
    // Verify
    assert(data == decompressed_buffer);
    
    return result;
}

BenchmarkResult compress_brotli(const std::string& dataset_name, const std::vector<uint8_t>& data, const int compression_level) {
    const double data_size_mb = data.size() / (1024.0 * 1024.0);

    BenchmarkResult result;
    result.dataset_name = dataset_name;
    result.compressor_name = "brotli -" + std::to_string(compression_level);
    result.random_access_speed = 0.0;
    result.average_random_access_time = 0.0;
       
    // Prepare compression buffer
    size_t compressed_size_bound = BrotliEncoderMaxCompressedSize(data.size());
    std::vector<uint8_t> compressed_buffer(compressed_size_bound);
    size_t encoded_size = compressed_size_bound;
    
    // Compress
    auto start = std::chrono::high_resolution_clock::now();
    
    if (!BrotliEncoderCompress(
            compression_level,
            BROTLI_DEFAULT_WINDOW,
            BROTLI_DEFAULT_MODE,
            data.size(),
            data.data(),
            &encoded_size,
            compressed_buffer.data())) {
        throw std::runtime_error("Brotli compression failed");
    }
    
    compressed_buffer.resize(encoded_size);
    
    auto compression_duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count();
    result.compression_speed = data_size_mb / compression_duration;
    result.compression_rate = static_cast<double>(data.size()) / static_cast<double>(compressed_buffer.size());
    
    // Decompress
    std::vector<uint8_t> decompressed_buffer(data.size());
    size_t decoded_size = data.size();
    
    start = std::chrono::high_resolution_clock::now();
    
    if (!BrotliDecoderDecompress(
            encoded_size,
            compressed_buffer.data(),
            &decoded_size,
            decompressed_buffer.data())) {
        throw std::runtime_error("Brotli decompression failed");
    }
    
    auto decompression_duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count();
    result.decompression_speed = data_size_mb / decompression_duration;
    
    // Verify
    assert(data == decompressed_buffer);
    
    return result;
}

BenchmarkResult compress_zstd(const std::string& dataset_name, const std::vector<uint8_t>& data, const int compression_level) {
    const double data_size_mb = data.size() / (1024.0 * 1024.0);

    BenchmarkResult result;
    result.dataset_name = dataset_name;
    result.compressor_name = "zstd -" + std::to_string(compression_level);
    result.random_access_speed = 0.0;
    result.average_random_access_time = 0.0;

    // Prepare compression buffer
    size_t max_dst_size = ZSTD_compressBound(data.size());
    std::vector<uint8_t> compressed_buffer(max_dst_size);
    
    // Compress
    auto start = std::chrono::high_resolution_clock::now();
    size_t compressed_size = ZSTD_compress(
        compressed_buffer.data(), compressed_buffer.size(),
        data.data(), data.size(),
        compression_level
    );
    
    if (ZSTD_isError(compressed_size)) {
        throw std::runtime_error("Zstd compression failed: " + std::string(ZSTD_getErrorName(compressed_size)));
    }
    compressed_buffer.resize(compressed_size);
    
    auto compression_duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count();
    result.compression_speed = data_size_mb / compression_duration;
    result.compression_rate = static_cast<double>(data.size()) / static_cast<double>(compressed_size);

    // Decompress
    std::vector<uint8_t> decompressed_buffer(data.size());
    start = std::chrono::high_resolution_clock::now();
    
    size_t decompressed_size = ZSTD_decompress(
        decompressed_buffer.data(), decompressed_buffer.size(),
        compressed_buffer.data(), compressed_size
    );
    
    if (ZSTD_isError(decompressed_size)) {
        throw std::runtime_error("Zstd decompression failed: " + std::string(ZSTD_getErrorName(decompressed_size)));
    }
    
    auto decompression_duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count();
    result.decompression_speed = data_size_mb / decompression_duration;
    
    // Verify
    assert(data == decompressed_buffer);
    
    return result;
}

BenchmarkResult compress_lz4(const std::string& dataset_name, const std::vector<uint8_t>& data, const int compression_level) {
    const double data_size_mb = data.size() / (1024.0 * 1024.0);

    BenchmarkResult result;
    result.dataset_name = dataset_name;
    result.compressor_name = "lz4 -" + std::to_string(compression_level);
    result.random_access_speed = 0.0;
    result.average_random_access_time = 0.0;
    
    // Prepare compression buffer
    size_t max_dst_size = LZ4_compressBound(data.size());
    std::vector<uint8_t> compressed_buffer(max_dst_size);
    
    // Compress
    auto start = std::chrono::high_resolution_clock::now();
    int compressed_size;
    if (compression_level == 0) {
        // Use default LZ4 compression
        compressed_size = LZ4_compress_default(
            reinterpret_cast<const char*>(data.data()),
            reinterpret_cast<char*>(compressed_buffer.data()),
            data.size(),
            compressed_buffer.size()
        );
    } else {
        // Use LZ4-HC compression for higher levels
        compressed_size = LZ4_compress_HC (
            reinterpret_cast<const char*>(data.data()),
            reinterpret_cast<char*>(compressed_buffer.data()),
            data.size(),
            compressed_buffer.size(),
            compression_level
        );
    }
    
    if (compressed_size <= 0) {
        throw std::runtime_error("LZ4 compression failed");
    }
    compressed_buffer.resize(compressed_size);
    
    auto compression_duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count();
    result.compression_speed = data_size_mb / compression_duration;
    result.compression_rate = static_cast<double>(data.size()) / static_cast<double>(compressed_size);
    
    // Decompress
    std::vector<uint8_t> decompressed_buffer(data.size());
    start = std::chrono::high_resolution_clock::now();
    
    int decompressed_size = LZ4_decompress_safe(
        reinterpret_cast<const char*>(compressed_buffer.data()),
        reinterpret_cast<char*>(decompressed_buffer.data()),
        compressed_size,
        data.size()
    );
    
    if (decompressed_size <= 0) {
        throw std::runtime_error("LZ4 decompression failed");
    }
    
    auto decompression_duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count();
    result.decompression_speed = data_size_mb / decompression_duration;
    
    // Verify
    assert(data == decompressed_buffer);
    
    return result;
}

BenchmarkResult compress_snappy(const std::string& dataset_name, const std::vector<uint8_t>& data) {
    const double data_size_mb = data.size() / (1024.0 * 1024.0);

    BenchmarkResult result;
    result.dataset_name = dataset_name;
    result.compressor_name = "snappy";
    result.random_access_speed = 0.0;
    result.average_random_access_time = 0.0;
    
     // Prepare compression buffer
    size_t compressed_size_bound = snappy::MaxCompressedLength(data.size());
    std::vector<uint8_t> compressed_buffer(compressed_size_bound);
    size_t compressed_size;
    
    // Compress
    auto start = std::chrono::high_resolution_clock::now();
    
    snappy::RawCompress(
        reinterpret_cast<const char*>(data.data()),
        data.size(),
        reinterpret_cast<char*>(compressed_buffer.data()),
        &compressed_size
    );
    
    compressed_buffer.resize(compressed_size);
    
    auto compression_duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count();
    result.compression_speed = data_size_mb / compression_duration;
    result.compression_rate = static_cast<double>(data.size()) / static_cast<double>(compressed_size);
    
    // Decompress
    std::vector<uint8_t> decompressed_buffer(data.size());
    size_t decompressed_size;
    
    start = std::chrono::high_resolution_clock::now();
    
    if (!snappy::RawUncompress(
            reinterpret_cast<const char*>(compressed_buffer.data()),
            compressed_size,
            reinterpret_cast<char*>(decompressed_buffer.data()))) {
        throw std::runtime_error("Snappy decompression failed");
    }
    
    auto decompression_duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count();
    result.decompression_speed = data_size_mb / decompression_duration;
    
    // Verify
    assert(data == decompressed_buffer);
    
    return result;
}

BenchmarkResult compress_xz(const std::string& dataset_name, const std::vector<uint8_t>& data, const int compression_level) {
    const double data_size_mb = data.size() / (1024.0 * 1024.0);

    BenchmarkResult result;
    result.dataset_name = dataset_name;
    result.compressor_name = "xz -" + std::to_string(compression_level);
    result.random_access_speed = 0.0;
    result.average_random_access_time = 0.0;

    // Initialize LZMA encoder
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_easy_encoder(&strm, compression_level, LZMA_CHECK_CRC64);
    if (ret != LZMA_OK) {
        throw std::runtime_error("Failed to initialize LZMA encoder");
    }
    
    // Prepare compression buffer
    std::vector<uint8_t> compressed_buffer(data.size() * 2); // Initial size estimate
    
    // Compress
    strm.next_in = data.data();
    strm.avail_in = data.size();
    strm.next_out = compressed_buffer.data();
    strm.avail_out = compressed_buffer.size();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    while (true) {
        ret = lzma_code(&strm, LZMA_FINISH);
        
        if (ret == LZMA_STREAM_END) {
            break;
        }
        
        if (ret != LZMA_OK) {
            lzma_end(&strm);
            throw std::runtime_error("LZMA compression failed");
        }
        
        // If we need more space
        if (strm.avail_out == 0) {
            size_t old_size = compressed_buffer.size();
            compressed_buffer.resize(old_size * 2);
            strm.next_out = compressed_buffer.data() + old_size;
            strm.avail_out = old_size;
        }
    }
    
    compressed_buffer.resize(strm.total_out);
    auto compression_duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count();
    result.compression_speed = data_size_mb / compression_duration;
    result.compression_rate = static_cast<double>(data.size()) / static_cast<double>(compressed_buffer.size());
    
    lzma_end(&strm);
    
    // Decompress
    std::vector<uint8_t> decompressed_buffer(data.size());
    strm = LZMA_STREAM_INIT;
    
    ret = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
    if (ret != LZMA_OK) {
        throw std::runtime_error("Failed to initialize LZMA decoder");
    }
    
    strm.next_in = compressed_buffer.data();
    strm.avail_in = compressed_buffer.size();
    strm.next_out = decompressed_buffer.data();
    strm.avail_out = decompressed_buffer.size();
    
    start = std::chrono::high_resolution_clock::now();
    
    ret = lzma_code(&strm, LZMA_FINISH);
    if (ret != LZMA_STREAM_END) {
        lzma_end(&strm);
        throw std::runtime_error("LZMA decompression failed");
    }
    
    auto decompression_duration = std::chrono::duration<double>(std::chrono::high_resolution_clock::now() - start).count();
    result.decompression_speed = data_size_mb / decompression_duration;
    
    lzma_end(&strm);
    
    // Verify
    assert(data == decompressed_buffer);
    
    return result;
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0] << " <dataset_path> <compressor_name> <compression_level> <output_file> [core_id]\n";
        return 1;
    }

    std::filesystem::path dataset_path(argv[1]);
    std::string compressor_name(argv[2]);
    int compression_level = std::stoi(argv[3]);
    std::filesystem::path output_file(argv[4]);
    int core_id = (argc > 5) ? std::stoi(argv[5]) : DEFAULT_CORE_ID;

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

        // Initialize the compressor
        BenchmarkResult result;
        if (compressor_name == "deflate") {
            result = compress_deflate(dataset_name, data, compression_level);
        }
        else if (compressor_name == "brotli") {
            result = compress_brotli(dataset_name, data, compression_level);
        }
        else if (compressor_name == "zstd") {
            result = compress_zstd(dataset_name, data, compression_level);
        }
        else if (compressor_name == "lz4") {
            result = compress_lz4(dataset_name, data, compression_level);
        }
        else if (compressor_name == "snappy") {
            result = compress_snappy(dataset_name, data);
        }
        else if (compressor_name == "xz") {
            result = compress_xz(dataset_name, data, compression_level);
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
