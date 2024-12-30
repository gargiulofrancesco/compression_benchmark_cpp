#include <iostream>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <numeric>
#include <iomanip>
#include <cassert>
#include <stdexcept>
#include "dataset_loader.h"

// Compression libraries
#include <lz4.h>
#include <lz4hc.h>
#include <zstd.h>
#include <brotli/encode.h>
#include <brotli/decode.h>
#include <snappy.h>
#include <zlib.h>
#include <lzma.h>

namespace fs = std::filesystem;
using Clock = std::chrono::high_resolution_clock;

const size_t N_ITER = 10;

struct CompressionResult {
    double size;      // Size in MB
    double rate;      // Compression ratio
    double c_speed;   // Compression speed in MB/s
    double d_speed;   // Decompression speed in MB/s
};

// Helper to calculate average results
CompressionResult get_average(const std::vector<CompressionResult>& results) {
    CompressionResult avg{0, 0, 0, 0};
    for (const auto& result : results) {
        avg.size += result.size;
        avg.rate += result.rate;
        avg.c_speed += result.c_speed;
        avg.d_speed += result.d_speed;
    }
    
    size_t count = results.size();
    avg.size /= count;
    avg.rate /= count;
    avg.c_speed /= count;
    avg.d_speed /= count;
    
    return avg;
}

void print_results(const std::vector<CompressionResult>& results, const std::string& compressorName) {
    std::cout << "\n" << compressorName << "\n";
    for (const auto& result : results) {
        std::cout << std::fixed << std::setprecision(2)
                  << "size: " << result.size
                  << ", rate: " << std::setprecision(3) << result.rate
                  << ", comp speed: " << std::setprecision(2) << result.c_speed
                  << ", decomp speed: " << result.d_speed << "\n";
    }
    
    auto avg = get_average(results);
    std::cout << "AVERAGE. "
              << "size: " << std::setprecision(2) << avg.size
              << ", rate: " << std::setprecision(3) << avg.rate
              << ", comp speed: " << std::setprecision(2) << avg.c_speed
              << ", decomp speed: " << avg.d_speed << "\n";
}

CompressionResult compress_lz4(const std::filesystem::path& path, int level) {
    // Load dataset
    auto dataset = Dataset::load(path);
    auto [dataset_name, data, end_positions, queries] = process_dataset(dataset);
    const double data_size_mb = data.size() / (1024.0 * 1024.0);
    
    // Prepare compression buffer
    size_t max_dst_size = LZ4_compressBound(data.size());
    std::vector<uint8_t> compressed_data(max_dst_size);
    
    // Compress
    auto start = Clock::now();
    int compressed_size;
    if (level == 0) {
        // Use default LZ4 compression
        compressed_size = LZ4_compress_default(
            reinterpret_cast<const char*>(data.data()),
            reinterpret_cast<char*>(compressed_data.data()),
            data.size(),
            compressed_data.size()
        );
    } else {
        // Use LZ4-HC compression for higher levels
        compressed_size = LZ4_compress_HC (
            reinterpret_cast<const char*>(data.data()),
            reinterpret_cast<char*>(compressed_data.data()),
            data.size(),
            compressed_data.size(),
            level
        );
    }
    
    if (compressed_size <= 0) {
        throw std::runtime_error("LZ4 compression failed");
    }
    compressed_data.resize(compressed_size);
    
    auto compression_duration = std::chrono::duration<double>(Clock::now() - start).count();
    double compression_speed = data_size_mb / compression_duration;
    
    // Decompress
    std::vector<uint8_t> decompressed_data(data.size());
    start = Clock::now();
    
    int decompressed_size = LZ4_decompress_safe(
        reinterpret_cast<const char*>(compressed_data.data()),
        reinterpret_cast<char*>(decompressed_data.data()),
        compressed_size,
        data.size()
    );
    
    if (decompressed_size <= 0) {
        throw std::runtime_error("LZ4 decompression failed");
    }
    
    auto decompression_duration = std::chrono::duration<double>(Clock::now() - start).count();
    double decompression_speed = data_size_mb / decompression_duration;
    
    // Verify
    assert(data == decompressed_data);
    
    return CompressionResult{
        static_cast<double>(compressed_size) / (1024.0 * 1024.0),
        static_cast<double>(data.size()) / compressed_size,
        compression_speed,
        decompression_speed
    };
}

CompressionResult compress_zstd(const std::filesystem::path& path, int level) {
    // Load dataset
    auto dataset = Dataset::load(path);
    auto [dataset_name, data, end_positions, queries] = process_dataset(dataset);
    const double data_size_mb = data.size() / (1024.0 * 1024.0);
    
    // Prepare compression buffer
    size_t max_dst_size = ZSTD_compressBound(data.size());
    std::vector<uint8_t> compressed_data(max_dst_size);
    
    // Compress
    auto start = Clock::now();
    size_t compressed_size = ZSTD_compress(
        compressed_data.data(), compressed_data.size(),
        data.data(), data.size(),
        level
    );
    
    if (ZSTD_isError(compressed_size)) {
        throw std::runtime_error("Zstd compression failed: " + std::string(ZSTD_getErrorName(compressed_size)));
    }
    compressed_data.resize(compressed_size);
    
    auto compression_duration = std::chrono::duration<double>(Clock::now() - start).count();
    double compression_speed = data_size_mb / compression_duration;
    
    // Decompress
    std::vector<uint8_t> decompressed_data(data.size());
    start = Clock::now();
    
    size_t decompressed_size = ZSTD_decompress(
        decompressed_data.data(), decompressed_data.size(),
        compressed_data.data(), compressed_size
    );
    
    if (ZSTD_isError(decompressed_size)) {
        throw std::runtime_error("Zstd decompression failed: " + std::string(ZSTD_getErrorName(decompressed_size)));
    }
    
    auto decompression_duration = std::chrono::duration<double>(Clock::now() - start).count();
    double decompression_speed = data_size_mb / decompression_duration;
    
    // Verify
    assert(data == decompressed_data);
    
    return CompressionResult{
        static_cast<double>(compressed_size) / (1024.0 * 1024.0),
        static_cast<double>(data.size()) / compressed_size,
        compression_speed,
        decompression_speed
    };
}

CompressionResult compress_deflate(const std::filesystem::path& path, int level) {
    // Load dataset
    auto dataset = Dataset::load(path);
    auto [dataset_name, data, end_positions, queries] = process_dataset(dataset);
    const double data_size_mb = data.size() / (1024.0 * 1024.0);
    
    // Prepare compression buffer
    std::vector<uint8_t> compressed_data(data.size() * 2);  // Should be enough
    
    // Compress
    z_stream strm{};
    deflateInit(&strm, level);
    
    strm.next_in = const_cast<uint8_t*>(data.data());
    strm.avail_in = data.size();
    strm.next_out = compressed_data.data();
    strm.avail_out = compressed_data.size();
    
    auto start = Clock::now();
    if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
        deflateEnd(&strm);
        throw std::runtime_error("Deflate compression failed");
    }
    
    size_t compressed_size = compressed_data.size() - strm.avail_out;
    compressed_data.resize(compressed_size);
    deflateEnd(&strm);
    
    auto compression_duration = std::chrono::duration<double>(Clock::now() - start).count();
    double compression_speed = data_size_mb / compression_duration;
    
    // Decompress
    std::vector<uint8_t> decompressed_data(data.size());
    inflateInit(&strm);
    
    strm.next_in = compressed_data.data();
    strm.avail_in = compressed_size;
    strm.next_out = decompressed_data.data();
    strm.avail_out = decompressed_data.size();
    
    start = Clock::now();
    if (inflate(&strm, Z_FINISH) != Z_STREAM_END) {
        inflateEnd(&strm);
        throw std::runtime_error("Deflate decompression failed");
    }
    
    inflateEnd(&strm);
    
    auto decompression_duration = std::chrono::duration<double>(Clock::now() - start).count();
    double decompression_speed = data_size_mb / decompression_duration;
    
    // Verify
    assert(data == decompressed_data);
    
    return CompressionResult{
        static_cast<double>(compressed_size) / (1024.0 * 1024.0),
        static_cast<double>(data.size()) / compressed_size,
        compression_speed,
        decompression_speed
    };
}

CompressionResult compress_xz(const std::filesystem::path& path, int level) {
    // Load dataset
    auto dataset = Dataset::load(path);
    auto [dataset_name, data, end_positions, queries] = process_dataset(dataset);
    const double data_size_mb = data.size() / (1024.0 * 1024.0);
    
    // Initialize LZMA encoder
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_easy_encoder(&strm, level, LZMA_CHECK_CRC64);
    if (ret != LZMA_OK) {
        throw std::runtime_error("Failed to initialize LZMA encoder");
    }
    
    // Prepare compression buffer
    std::vector<uint8_t> compressed_data(data.size() * 2); // Initial size estimate
    
    // Compress
    strm.next_in = data.data();
    strm.avail_in = data.size();
    strm.next_out = compressed_data.data();
    strm.avail_out = compressed_data.size();
    
    auto start = Clock::now();
    
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
            size_t old_size = compressed_data.size();
            compressed_data.resize(old_size * 2);
            strm.next_out = compressed_data.data() + old_size;
            strm.avail_out = old_size;
        }
    }
    
    compressed_data.resize(strm.total_out);
    auto compression_duration = std::chrono::duration<double>(Clock::now() - start).count();
    double compression_speed = data_size_mb / compression_duration;
    
    lzma_end(&strm);
    
    // Decompress
    std::vector<uint8_t> decompressed_data(data.size());
    strm = LZMA_STREAM_INIT;
    
    ret = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
    if (ret != LZMA_OK) {
        throw std::runtime_error("Failed to initialize LZMA decoder");
    }
    
    strm.next_in = compressed_data.data();
    strm.avail_in = compressed_data.size();
    strm.next_out = decompressed_data.data();
    strm.avail_out = decompressed_data.size();
    
    start = Clock::now();
    
    ret = lzma_code(&strm, LZMA_FINISH);
    if (ret != LZMA_STREAM_END) {
        lzma_end(&strm);
        throw std::runtime_error("LZMA decompression failed");
    }
    
    auto decompression_duration = std::chrono::duration<double>(Clock::now() - start).count();
    double decompression_speed = data_size_mb / decompression_duration;
    
    lzma_end(&strm);
    
    // Verify
    assert(data == decompressed_data);
    
    return CompressionResult{
        static_cast<double>(compressed_data.size()) / (1024.0 * 1024.0),
        static_cast<double>(data.size()) / compressed_data.size(),
        compression_speed,
        decompression_speed
    };
}

CompressionResult compress_snappy(const std::filesystem::path& path) {
    // Load dataset
    auto dataset = Dataset::load(path);
    auto [dataset_name, data, end_positions, queries] = process_dataset(dataset);
    const double data_size_mb = data.size() / (1024.0 * 1024.0);
    
    // Prepare compression buffer
    size_t compressed_size_bound = snappy::MaxCompressedLength(data.size());
    std::vector<uint8_t> compressed_data(compressed_size_bound);
    size_t compressed_size;
    
    // Compress
    auto start = Clock::now();
    
    snappy::RawCompress(
        reinterpret_cast<const char*>(data.data()),
        data.size(),
        reinterpret_cast<char*>(compressed_data.data()),
        &compressed_size
    );
    
    compressed_data.resize(compressed_size);
    
    auto compression_duration = std::chrono::duration<double>(Clock::now() - start).count();
    double compression_speed = data_size_mb / compression_duration;
    
    // Decompress
    std::vector<uint8_t> decompressed_data(data.size());
    size_t decompressed_size;
    
    start = Clock::now();
    
    if (!snappy::RawUncompress(
            reinterpret_cast<const char*>(compressed_data.data()),
            compressed_size,
            reinterpret_cast<char*>(decompressed_data.data()))) {
        throw std::runtime_error("Snappy decompression failed");
    }
    
    auto decompression_duration = std::chrono::duration<double>(Clock::now() - start).count();
    double decompression_speed = data_size_mb / decompression_duration;
    
    // Verify
    assert(data == decompressed_data);
    
    return CompressionResult{
        static_cast<double>(compressed_data.size()) / (1024.0 * 1024.0),
        static_cast<double>(data.size()) / compressed_data.size(),
        compression_speed,
        decompression_speed
    };
}

CompressionResult compress_brotli(const std::filesystem::path& path, int level) {
    // Load dataset
    auto dataset = Dataset::load(path);
    auto [dataset_name, data, end_positions, queries] = process_dataset(dataset);
    const double data_size_mb = data.size() / (1024.0 * 1024.0);
    
    // Prepare compression buffer
    size_t compressed_size_bound = BrotliEncoderMaxCompressedSize(data.size());
    std::vector<uint8_t> compressed_data(compressed_size_bound);
    size_t encoded_size = compressed_size_bound;
    
    // Compress
    auto start = Clock::now();
    
    if (!BrotliEncoderCompress(
            level,
            BROTLI_DEFAULT_WINDOW,
            BROTLI_DEFAULT_MODE,
            data.size(),
            data.data(),
            &encoded_size,
            compressed_data.data())) {
        throw std::runtime_error("Brotli compression failed");
    }
    
    compressed_data.resize(encoded_size);
    
    auto compression_duration = std::chrono::duration<double>(Clock::now() - start).count();
    double compression_speed = data_size_mb / compression_duration;
    
    // Decompress
    std::vector<uint8_t> decompressed_data(data.size());
    size_t decoded_size = data.size();
    
    start = Clock::now();
    
    if (!BrotliDecoderDecompress(
            encoded_size,
            compressed_data.data(),
            &decoded_size,
            decompressed_data.data())) {
        throw std::runtime_error("Brotli decompression failed");
    }
    
    auto decompression_duration = std::chrono::duration<double>(Clock::now() - start).count();
    double decompression_speed = data_size_mb / decompression_duration;
    
    // Verify
    assert(data == decompressed_data);
    
    return CompressionResult{
        static_cast<double>(compressed_data.size()) / (1024.0 * 1024.0),
        static_cast<double>(data.size()) / compressed_data.size(),
        compression_speed,
        decompression_speed
    };
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Error: Missing directory argument. Usage: " << argv[0] << " <directory>\n";
        return 1;
    }
    
    fs::path dir(argv[1]);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::cerr << "Error: " << argv[1] << " is not a valid directory.\n";
        return 1;
    }
    
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        
        const std::filesystem::path path = entry.path();
        std::cout << "\nProcessing: " << path.string() << "\n";
        
        try {            
            // LZ4 Default
            {
                std::vector<CompressionResult> results;
                for (size_t i = 0; i < N_ITER; ++i) {
                    results.push_back(compress_lz4(path, 0));
                }
                print_results(results, "LZ4 Default");
            }
            
            // LZ4HC
            for (int level : {1, 3, 6, 9, 12}) {
                std::vector<CompressionResult> results;
                for (size_t i = 0; i < N_ITER; ++i) {
                    results.push_back(compress_lz4(path, level));
                }
                print_results(results, "LZ4HC -" + std::to_string(level));
            }
            
            // Zstd
            for (int level : {1, 3, 6, 9, 12}) {
                std::vector<CompressionResult> results;
                for (size_t i = 0; i < N_ITER; ++i) {
                    results.push_back(compress_zstd(path, level));
                }
                print_results(results, "Zstd -" + std::to_string(level));
            }
            
            // Deflate
            for (int level : {1, 6, 9}) {
                std::vector<CompressionResult> results;
                for (size_t i = 0; i < N_ITER; ++i) {
                    results.push_back(compress_deflate(path, level));
                }
                print_results(results, "deflate -" + std::to_string(level));
            }
            
            // XZ
            for (int level : {1, 3}) {
                std::vector<CompressionResult> results;
                for (size_t i = 0; i < N_ITER; ++i) {
                    results.push_back(compress_xz(path, level));
                }
                print_results(results, "xz -" + std::to_string(level));
            }
            
            // Brotli
            for (int level : {1, 3, 6}) {
                std::vector<CompressionResult> results;
                for (size_t i = 0; i < N_ITER; ++i) {
                    results.push_back(compress_brotli(path, level));
                }
                print_results(results, "brotli -" + std::to_string(level));
            }
            
            // Snappy
            {
                std::vector<CompressionResult> results;
                for (size_t i = 0; i < N_ITER; ++i) {
                    results.push_back(compress_snappy(path));
                }
                print_results(results, "snappy");
            }

        } catch (const std::exception& e) {
            std::cerr << "Error processing " << entry.path() << ": " << e.what() << "\n";
            continue;
        }
    }
    
    return 0;
}