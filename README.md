# Compression Benchmark

## Setup

To set up and run the compression benchmark, follow the steps below:

### Steps to Build and Run:

0. **Prerequisites**
To run the benchmark, you must install the required libraries using vcpkg.
```bash
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.sh
```
You will need to install the following libraries:
```bash
./vcpkg install simdjson brotli zlib lz4 liblzma zstd snappy
```

1. **Clone the repository**:
```bash
git clone --recurse-submodules https://github.com/gargiulofrancesco/compression_benchmark_cpp.git
cd compression_benchmark_cpp
```

2. **Create a build folder and navigate into it**
```bash
mkdir build
cd build
```

3. **Run CMake to configure the project:**
```bash
cmake -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release ..
```

4. **Build the project:**
```bash
cmake --build . --config Release
```

5. **Run the benchmark executable:**
```bash
./benchmark_all /path/to/datasets_directory
```
