cmake_minimum_required(VERSION 3.25)
project(fastregex_compile_fail LANGUAGES CXX)

add_executable(compile_fail "${CASE_SOURCE}")
target_include_directories(
    compile_fail PRIVATE "${FASTREGEX_SOURCE_DIR}/include" "${FASTREGEX_SOURCE_DIR}/src")
target_compile_features(compile_fail PRIVATE cxx_std_23)
set_target_properties(compile_fail PROPERTIES CXX_EXTENSIONS OFF)
