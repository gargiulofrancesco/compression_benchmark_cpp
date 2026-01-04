# OnPair Compression Benchmark Suite

[![Paper](https://img.shields.io/badge/Paper-arXiv:2508.02280-blue)](https://arxiv.org/abs/2508.02280)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

This repository contains the C++ **experimental evaluation framework** for the paper:

> **[OnPair: Short Strings Compression for Fast Random Access](https://arxiv.org/abs/2508.02280)**  

## Overview

**OnPair** is a compression algorithm specifically designed for workloads requiring fast random access to individual strings in large collections. This benchmark suite provides comprehensive performance evaluation tools to compare OnPair against established compression methods.

For the **standalone OnPair algorithm implementation**, see: **[onpair_cpp](https://github.com/gargiulofrancesco/onpair_cpp)** 

## Quick Start

### Installation

```bash
git clone --recurse-submodules https://github.com/gargiulofrancesco/compression_benchmark_cpp.git
cd compression_benchmark_cpp
```

### Prerequisites

To run the benchmark, you must install the required libraries using vcpkg:

```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
./vcpkg install simdjson brotli zlib lz4 liblzma zstd snappy
```

### Building

```bash
# Create build directory
mkdir build
cd build

# Configure with CMake (adjust vcpkg path as needed)
cmake -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release ..

# Build with release optimizations
cmake --build . --config Release
```

### Datasets

To reproduce the experiments presented in the paper, use the provided Python script to download and process the standard datasets:

```bash
# Create a virtual environment
python -m venv venv
source venv/bin/activate

# Install dependencies
pip install -r scripts/requirements.txt

# Download and process datasets
python scripts/process_datasets.py
```

The script will download and process the datasets into the JSON format required by the benchmark suite. The datasets will be saved in the `data/` directory.

### Running Benchmarks

#### Single Algorithm Evaluation
Evaluate a specific algorithm on a dataset:

```bash
./benchmark_individual <dataset.json> <algorithm> <output.json> [core_id]
```

**Example:**
```bash
# Run OnPair on example dataset with CPU core pinning
./benchmark_individual data/example.json onpair results.json 0
```

#### Comprehensive Benchmark Suite
Run all algorithms on all datasets in a directory:

```bash
./benchmark_all <dataset_directory> [core_id]
```

**Examples:**
```bash
# Run complete benchmark suite with CPU core pinning
./benchmark_all data/ 0
```

This generates a comprehensive performance comparison across all algorithms and datasets.

#### Dictionary Efficiency Analysis
Compare the training efficiency (latency and compression ratio) of OnPair and Sampled BPE on the same sample:

```bash
./dictionary_efficiency <dataset.json> [core_id]
```

**Example:**
```bash
# Run efficiency analysis on book titles
./dictionary_efficiency data/book_titles.json 0
```

This tool runs a series of experiments varying the OnPair frequency threshold (2-15) and compares it against Sampled BPE using the exact same sample size. It outputs a table showing sample size, training time, and compression ratio for both methods.


## Supported Algorithms

| Algorithm | Description |
|-----------|-------------|
| `raw` | Uncompressed baseline |
| `brotli` | Google's Brotli compression |
| `deflate` | DEFLATE compression algorithm |
| `lz4` | LZ4 fast compression |
| `snappy` | Google's Snappy compression |
| `xz` | XZ compression |
| `zstd` | Facebook's Zstandard compression |
| `bpe` | Byte Pair Encoding |
| `sampled_bpe` | Sampled Byte Pair Encoding |
| `fsst` | Fast Static Symbol Table compression |
| `onpair` | OnPair algorithm |

## Dataset Format

Datasets must be JSON arrays of strings:

```json
[
   "user_12345",
   "admin_67890", 
   "guest_11111",
   "user_54321"
]
```

## Performance Metrics

The benchmark suite evaluates algorithms across four key dimensions:

| Metric | Description | Units |
|--------|-------------|-------|
| **Compression Ratio** | `original_size / compressed_size` | Ratio |
| **Compression Speed** | Throughput during compression | MiB/s |
| **Decompression Speed** | Throughput during full decompression | MiB/s |
| **Random Access Time** | Average time per individual string access | nanoseconds |

**Output Format:** Results are exported as structured JSON for easy analysis and visualization.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Authors

- **Francesco Gargiulo** - [francesco.gargiulo@phd.unipi.it](mailto:francesco.gargiulo@phd.unipi.it)
- **Rossano Venturini** - [rossano.venturini@unipi.it](mailto:rossano.venturini@unipi.it)

*University of Pisa, Italy*
