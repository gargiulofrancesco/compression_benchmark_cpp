cmake_minimum_required(VERSION 3.10)

# Set the project name
project(CompressionBenchmark)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Libraries required for the project (installed via vcpkg)
find_package(simdjson CONFIG REQUIRED)
find_package(unofficial-brotli CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(liblzma CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(Snappy CONFIG REQUIRED)

# Add the external FSST submodule
add_subdirectory(external/fsst)

# Add the external OnPair submodule
add_subdirectory(external/onpair)

# Define a reusable variable for include directories
set(PROJECT_INCLUDE_DIRS
    include/compressors
    include/utils
    external/fsst
    external/robin_hood
    external/onpair/include
)

# Source files
file(GLOB PROJECT_EXECUTABLES src/executables/*.cpp) 
file(GLOB COMPRESSOR_SOURCES src/compressors/*.cpp)
file(GLOB UTILS_SOURCES src/utils/*.cpp)
set(COMMON_SOURCES ${COMPRESSOR_SOURCES} ${UTILS_SOURCES})

# Create targets with common properties
foreach(executable_path ${PROJECT_EXECUTABLES})
    # Get the target name by removing the path and the extension
    get_filename_component(executable_name ${executable_path} NAME_WE)
    
    # Create the executable target
    add_executable(${executable_name} ${executable_path} ${COMMON_SOURCES})
    
    # Include directories and link libraries
    target_include_directories(${executable_name} PRIVATE ${PROJECT_INCLUDE_DIRS})
    target_link_libraries(${executable_name} PRIVATE 
        simdjson::simdjson
        fsst
        onpair
        unofficial::brotli::brotlidec 
        unofficial::brotli::brotlienc
        ZLIB::ZLIB
        lz4::lz4
        liblzma::liblzma
        zstd::libzstd
        Snappy::snappy
    )
endforeach()

# Define a reusable variable for optimization flags
if(NOT MSVC)
    set(OPTIMIZATION_FLAGS "-O3 -DNDEBUG -flto")
else()
    set(OPTIMIZATION_FLAGS "/O2 /DNDEBUG /GL")
endif()

# Set release-specific compiler and linker flags
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${OPTIMIZATION_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} ${OPTIMIZATION_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} ${OPTIMIZATION_FLAGS}")

# Enable inter-procedural optimizations if supported (modern CMake)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    foreach(executable_path ${PROJECT_EXECUTABLES})
        # Get the target name by removing the path and the extension
        get_filename_component(executable_name ${executable_path} NAME_WE)
        
        # Enable IPO (LTO) for the target
        set_property(TARGET ${executable_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endforeach()
else()
    message(WARNING "IPO (LTO) is not supported: ${ipo_error}")
endif()

# Check for `-march=native` support and append it if supported
check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
if(COMPILER_SUPPORTS_MARCH_NATIVE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
endif()

# Include CheckCXXCompilerFlag to detect compiler support for flags
include(CheckCXXCompilerFlag)

# Set a default build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Release" FORCE)
endif()

# Specify output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
