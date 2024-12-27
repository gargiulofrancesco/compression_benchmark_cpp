cmake_minimum_required(VERSION 3.10)

# Set the project name
project(CompressionBenchmark)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the external FSST submodule
add_subdirectory(external/fsst)

# Define a reusable variable for include directories
set(PROJECT_INCLUDE_DIRS
    include             # Project headers
    external/fsst       # External FSST headers
    external/json       # nlohmann/json
    external/robin_hood # robin_hood hashing
)

# Define a reusable variable for optimization flags
if(NOT MSVC)
    set(OPTIMIZATION_FLAGS "-O3 -DNDEBUG -flto")
else()
    set(OPTIMIZATION_FLAGS "/O2 /DNDEBUG /GL")
endif()

# Add the source directory and define the executables
add_executable(run_single_benchmark
    src/run_single_benchmark.cpp
    src/copy_compressor.cpp
    src/fsst_compressor.cpp
    src/lz4_compressor.cpp
    src/onpair16_compressor.cpp
    src/dataset_loader.cpp
)

add_executable(run_benchmark
    src/run_benchmark.cpp
    src/copy_compressor.cpp
    src/fsst_compressor.cpp
    src/lz4_compressor.cpp
    src/onpair16_compressor.cpp
    src/dataset_loader.cpp
)

add_executable(test_correctness
    src/test_correctness.cpp
    src/copy_compressor.cpp
    src/fsst_compressor.cpp
    src/lz4_compressor.cpp
    src/onpair16_compressor.cpp
    src/dataset_loader.cpp
)

# Include directories for executables
target_include_directories(run_single_benchmark PRIVATE ${PROJECT_INCLUDE_DIRS})
target_include_directories(run_benchmark PRIVATE ${PROJECT_INCLUDE_DIRS})
target_include_directories(test_correctness PRIVATE ${PROJECT_INCLUDE_DIRS})

# Link the FSST library
target_link_libraries(run_single_benchmark PRIVATE fsst)
target_link_libraries(run_benchmark PRIVATE fsst)
target_link_libraries(test_correctness PRIVATE fsst)

# Include CheckCXXCompilerFlag to detect compiler support for flags
include(CheckCXXCompilerFlag)

# Check for `-march=native` support and append it if supported
check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if(COMPILER_SUPPORTS_MARCH_NATIVE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
endif()

# Set release-specific compiler and linker flags
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${OPTIMIZATION_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} ${OPTIMIZATION_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${OPTIMIZATION_FLAGS}")

# Enable inter-procedural optimizations if supported (modern CMake)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set_property(TARGET run_single_benchmark PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_property(TARGET run_benchmark PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_property(TARGET test_correctness PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(WARNING "IPO (LTO) is not supported: ${ipo_error}")
endif()

# Set a default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Release" FORCE)
endif()

# Specify output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
