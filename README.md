# String Compression for Random Access and Prefix Filtering

This repository contains the experimental framework accompanying our SIGIR 2026 submission. All results presented in the paper are fully reproducible using the code and scripts provided here.

## Paper Abstract
Modern information retrieval systems store billions of document metadata strings (titles, URLs, descriptions) in memory to meet strict latency targets. Compressing this data is essential, but existing methods force difficult trade-offs: block-based schemes like LZ77 require decompressing entire blocks for single-string access; Byte-Pair Encoding (BPE) achieves strong compression but incurs prohibitive training costs; and Fast Static Symbol Table (FSST) prioritizes encoding throughput at the expense of compression ratio.

We present OnPair, a dictionary-based compressor that achieves compression ratios competitive with BPE while demonstrating dramatically faster construction. OnPair uses a single-pass, cache-friendly algorithm that incrementally merges frequent token pairs based  on local frequency thresholds, avoiding BPE's global frequency updates. Each string is compressed independently through greedy parsing, enabling efficient random access. By lexicographically sorting the dictionary, OnPair evaluates prefix predicates directly on compressed data, eliminating decompression overhead during query execution while preserving compression ratio.

Evaluated on four real-world datasets, OnPair constructs dictionaries on average $22\times$ faster than BPE on identical training samples while increasing the compressed footprint by less than $1\%$. The sorted dictionary enables direct compressed-domain prefix filtering, accelerating scans by up to $3\times$ compared to optimized uncompressed baselines.

## Reproducibility

### 1. Clone and Setup

```bash
# Clone the repository
git clone <benchmark-url>
cd benchmark

# Clone vcpkg for dependency management
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh

# Install required libraries
./vcpkg/vcpkg install simdjson brotli zlib lz4 liblzma zstd snappy
```

### 2. Building

```bash
# Create build directory
mkdir build

# Configure with CMake
cmake -DCMAKE_TOOLCHAIN_FILE=${PWD}/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -S . -B build

# Build with optimizations
cmake --build build --config Release
```

### 3. Download and Process Datasets

```bash
# Set up Python environment
python -m venv venv
source venv/bin/activate
pip install -r scripts/requirements.txt

# Download and process all datasets used in the paper
python scripts/process_datasets.py
```

This will create four datasets in `data/`:
- `amazon.json` - Amazon book titles 
- `query_logs.json` - MS MARCO queries 
- `urls.json` - MS MARCO Web URLs 
- `wikipedia.json` - DBpedia abstracts

## Reproducing Paper Results

### Compression Benchmark (Table 2)

Run the comprehensive benchmark suite across all algorithms and datasets:

```bash
# Run complete benchmark suite with CPU core pinning
./build/benchmark_all data/ 0
```

This benchmark eports four key metrics aligned with the paper:

| Metric | Description | Units |
|--------|-------------|-------|
| **Compression Ratio** | `original_size / compressed_size` | Ratio |
| **Compression Speed** | Throughput during compression | MiB/s |
| **Decompression Speed** | Throughput during full decompression | MiB/s |
| **Random Access Time** | Average time per individual string access | nanoseconds |

**Output Format:** Results are exported as structured JSON for easy analysis and visualization.

### Prefix Filtering Benchmark (Figure 6)

Evaluate OnPair's compressed prefix filtering against an optimized baseline (Raw) and a control variable (OnPair with unsorted dictionary).

```bash
# Run prefix filtering benchmark on the Amazon dataset with CPU core pinning
./build/prefix_filtering data/amazon.json 0
```

This benchmark measures the latency of prefix queries (e.g., SQL `LIKE 'ex%'`), reporting results for each algorithm.

### Dictionary Construction Efficiency (Figure 2)

Compare the training efficiency (latency and compression ratio) of OnPair and Sampled BPE on the same sample:

```bash
# Run efficiency analysis on the Amazon dataset with core pinning
./build/dictionary_efficiency data/amazon.json 0
```

This tool runs a series of experiments varying the OnPair frequency threshold (2-15) and compares it against Sampled BPE using the exact same sample. It outputs a table showing sample size, training time, and compression ratio for both methods.


## Supported Algorithms 

Our benchmark includes implementations of all baselines discussed in the paper (and more).

**General-Purpose Compressor** (Block-based, 64 KiB blocks):
- `lz4` - LZ4 fast compression
- `zstd` - Zstandard (Facebook)
- `brotli` - Brotli (Google)
- `deflate` - DEFLATE algorithm
- `xz` - LZMA2-based compression
- `snappy` - Snappy (Google)

**Dictionary-Based Compressors** (Field-level):
- `bpe` - Byte-Pair Encoding (full corpus, 2^16 tokens)
- `sampled_bpe` - BPE with sampling + greedy parsing (2^16 tokens)
- `fsst` - Fast Static Symbol Table (2^8 tokens, 8-byte max)
- `onpair` - **Our method** (2^16 tokens)

**Specialized Methods**:
- `front_coding` - Front Coding (requires sorted input)
- `raw` - Uncompressed baseline

## Dataset Format

All datasets follow a simple JSON array format:

```json
[
   "String-based Web Search: A DB Approach",
   "Efficient Query Evaluation using a Two-Level Retrieval Process",
   "Learning to Rank for Information Retrieval"
]
```

Custom datasets can be added by following this format.

## Extending the Framework

### Adding New Compression Algorithms

1. Implement the `Compressor` interface in `src/compressors/`
2. Register in `src/compressors/compressor_factory.cpp`
3. Rebuild and run with algorithm name

### Adding New Datasets

1. Create JSON file following the format above
2. Place in `data/` directory
3. Run benchmarks as normal
