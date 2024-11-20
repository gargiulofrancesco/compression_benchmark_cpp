cmake_minimum_required(VERSION 3.10)

# Set the project name
project(CompressionBenchmark)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the external FSST submodule
add_subdirectory(external/fsst)

# Add the source directory
add_executable(run_benchmark
    src/main.cpp
    src/fsst_compressor.cpp
)

# Include directories for project headers
target_include_directories(run_benchmark PRIVATE
    include          # Project headers
    external/fsst    # External FSST headers
)

# Link the FSST library
target_link_libraries(run_benchmark PRIVATE fsst)

# Compiler flags for release builds
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")