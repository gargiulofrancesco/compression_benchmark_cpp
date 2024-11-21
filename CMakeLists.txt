cmake_minimum_required(VERSION 3.10)

# Set the project name
project(CompressionBenchmark)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the external FSST submodule
add_subdirectory(external/fsst)

# Add the source directory and define the executable
add_executable(run_benchmark
    src/run_benchmark.cpp
    src/fsst_compressor.cpp
    src/dataset_loader.cpp
)

# Include directories for project headers
target_include_directories(run_benchmark PRIVATE
    include          # Project headers
    external/fsst    # External FSST headers
    external/json    # nlohmann/json
)

# Link the FSST library
target_link_libraries(run_benchmark PRIVATE fsst)

# Include CheckCXXCompilerFlag to detect compiler support for flags
include(CheckCXXCompilerFlag)

# Check for `-march=native` support and append it if supported
check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if(COMPILER_SUPPORTS_MARCH_NATIVE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
endif()

# Set release-specific compiler flags and enable Link-Time Optimization (LTO)
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG ${CMAKE_CXX_FLAGS_RELEASE} -flto")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -flto")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -flto")
else()
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG ${CMAKE_CXX_FLAGS_RELEASE} /GL")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
endif()

# Enable inter-procedural optimizations if supported (modern CMake)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set_property(TARGET run_benchmark PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(WARNING "IPO (LTO) is not supported: ${ipo_error}")
endif()

# Set a default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Release" FORCE)
endif()


# Specify output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)