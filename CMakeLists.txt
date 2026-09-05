cmake_minimum_required(VERSION 3.15)

project(libigrf
    VERSION 1.0.0
    DESCRIPTION "Small, portable, dependency-free IGRF C library"
    LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Default to an optimized build; performance-sensitive users should not
# have to know to pass -DCMAKE_BUILD_TYPE=Release.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# The library builds as a static library by default; configure with
# -DBUILD_SHARED_LIBS=ON to produce a shared library (.dll/.so) instead.
option(BUILD_SHARED_LIBS "Build libigrf as a shared library" OFF)
option(IGRF_BUILD_DEMO "Build the localhost web demo" ON)
option(IGRF_BUILD_TESTS "Build the test suite" ON)
option(IGRF_BUILD_EXAMPLES "Build the C examples" ON)
option(IGRF_BUILD_BENCHMARK "Build the benchmark tool" ON)

add_library(igrf
    src/igrf.c
    src/shc.c
    src/coordinates.c)
add_library(igrf::igrf ALIAS igrf)

target_include_directories(igrf
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

set_target_properties(igrf PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    POSITION_INDEPENDENT_CODE ON)

if(NOT BUILD_SHARED_LIBS AND MSVC)
    target_compile_definitions(igrf PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(MSVC)
    target_compile_options(igrf PRIVATE /W4)
else()
    target_compile_options(igrf PRIVATE -Wall -Wextra -pedantic)
endif()

# ------------------------------------------------------------------
# C example
# ------------------------------------------------------------------
if(IGRF_BUILD_EXAMPLES)
    add_executable(igrf_example examples/basic.c)
    target_link_libraries(igrf_example PRIVATE igrf::igrf)
endif()

# ------------------------------------------------------------------
# Localhost web demo (library consumer; separate from libigrf itself)
# ------------------------------------------------------------------
if(IGRF_BUILD_DEMO)
    add_executable(igrf_demo
        demo/main.c
        demo/http.c)
    target_link_libraries(igrf_demo PRIVATE igrf::igrf)
    if(WIN32)
        target_link_libraries(igrf_demo PRIVATE ws2_32)
    else()
        find_package(Threads)
        if(Threads_FOUND)
            target_link_libraries(igrf_demo PRIVATE Threads::Threads)
        endif()
    endif()

    # Copy the demo assets next to the build output so that
    # `igrf_demo` runs with its default paths from the build dir.
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION ${CMAKE_BINARY_DIR})
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/demo/web DESTINATION ${CMAKE_BINARY_DIR}/demo)
endif()

# ------------------------------------------------------------------
# Benchmark
# ------------------------------------------------------------------
if(IGRF_BUILD_BENCHMARK)
    add_executable(igrf_benchmark tools/benchmark.c)
    target_link_libraries(igrf_benchmark PRIVATE igrf::igrf)
endif()

# ------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------
if(IGRF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
