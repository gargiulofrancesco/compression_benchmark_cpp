# Compression Benchmark

## Setup

To set up and run the compression benchmark, follow the steps below:

### Prerequisites

- CMake 3.10 or higher
- A C++17 compatible compiler
- FSST library (included as a submodule)

### Steps to Build and Run:

1. **Clone the repository** (if you haven't already):
```bash
git clone <repository-url>
cd CompressionBenchmark
```

2. **Create a build folder and navigate into it**
```bash
mkdir build
cd build
```

3. **Run CMake to configure the project:**
```bash
cmake ..
```

4. **Build the project:**
```bash
cmake --build .
```

5. **Run the benchmark executable:**
```bash
./bin/run_benchmark
```