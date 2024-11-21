# Compression Benchmark

## Setup

To set up and run the compression benchmark, follow the steps below:

### Steps to Build and Run:

1. **Clone the repository** (if you haven't already):
```bash
git clone --recurse-submodules <repository-url>
cd compression_benchmark_cpp
```

2. **Create a build folder and navigate into it**
```bash
mkdir build
cd build
```

3. **Run CMake to configure the project:**
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
```

4. **Build the project:**
```bash
cmake --build . --config Release
```

5. **Run the benchmark executable:**
```bash
./run_benchmark
```