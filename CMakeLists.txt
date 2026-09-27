cmake_minimum_required(VERSION 3.25)

project(fastregex VERSION 0.1.0 LANGUAGES CXX)

# Keep project defaults local; do not change a parent project's language flags.
set(CMAKE_CXX_EXTENSIONS OFF)

option(FASTREGEX_BUILD_TESTS "Build fastregex component tests" ${PROJECT_IS_TOP_LEVEL})
option(FASTREGEX_BUILD_BENCHMARKS "Build fastregex benchmarks" OFF)

# Consumer headers deliberately do not expose compiler implementation headers.
add_library(fastregex INTERFACE)
add_library(fastregex::fastregex ALIAS fastregex)
target_include_directories(fastregex INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(fastregex INTERFACE cxx_std_11)

# Internal development target. Not part of the consumer API.
add_library(fastregex_compiler INTERFACE)
target_include_directories(fastregex_compiler INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(fastregex_compiler INTERFACE cxx_std_23)
target_link_libraries(fastregex_compiler INTERFACE fastregex::fastregex)

if(FASTREGEX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(FASTREGEX_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
