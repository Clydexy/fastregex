add_executable(fastregex_public_smoke public_smoke.cpp)
target_link_libraries(fastregex_public_smoke PRIVATE fastregex::fastregex)
set_target_properties(fastregex_public_smoke PROPERTIES CXX_STANDARD 11 CXX_STANDARD_REQUIRED YES)
add_test(NAME fastregex.public_smoke COMMAND fastregex_public_smoke)

add_executable(fastregex_compiler_smoke compiler_smoke.cpp)
target_link_libraries(fastregex_compiler_smoke PRIVATE fastregex_compiler)
add_test(NAME fastregex.compiler_smoke COMMAND fastregex_compiler_smoke)

add_executable(fastregex_parser_tests parser.cpp)
target_link_libraries(fastregex_parser_tests PRIVATE fastregex_compiler)
add_test(NAME fastregex.parser COMMAND fastregex_parser_tests)

add_executable(fastregex_nfa_tests nfa.cpp)
target_link_libraries(fastregex_nfa_tests PRIVATE fastregex_compiler)
add_test(NAME fastregex.nfa COMMAND fastregex_nfa_tests)

add_executable(fastregex_dfa_tests dfa.cpp)
target_link_libraries(fastregex_dfa_tests PRIVATE fastregex_compiler)
add_test(NAME fastregex.dfa COMMAND fastregex_dfa_tests)

add_executable(fastregex_dfa_match_tests dfa_match.cpp)
target_link_libraries(fastregex_dfa_match_tests PRIVATE fastregex_compiler)
add_test(NAME fastregex.dfa_match COMMAND fastregex_dfa_match_tests)

add_executable(fastregex_dfa_equivalence_tests dfa_equivalence.cpp)
target_link_libraries(fastregex_dfa_equivalence_tests PRIVATE fastregex_compiler)
add_test(NAME fastregex.dfa_equivalence COMMAND fastregex_dfa_equivalence_tests)

add_executable(fastregex_static_api_tests static_api.cpp)
target_link_libraries(fastregex_static_api_tests PRIVATE fastregex::compile_time)
add_test(NAME fastregex.static_api COMMAND fastregex_static_api_tests)

add_executable(fastregex_phase1_regression_tests phase1_regression.cpp)
target_link_libraries(fastregex_phase1_regression_tests PRIVATE fastregex::compile_time)
add_test(NAME fastregex.phase1_regression COMMAND fastregex_phase1_regression_tests)

function(fastregex_expect_compile_failure name source diagnostic offset)
    try_compile(
        compiled
        "${CMAKE_CURRENT_BINARY_DIR}/compile_fail/${name}"
        "${CMAKE_CURRENT_SOURCE_DIR}/compile_fail"
        fastregex_compile_fail
        compile_fail
        CMAKE_FLAGS
            "-DFASTREGEX_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DCASE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/${source}"
        OUTPUT_VARIABLE output)
    if(compiled)
        message(FATAL_ERROR "${name} unexpectedly compiled")
    endif()
    if(NOT output MATCHES "fastregex: ${diagnostic}")
        message(FATAL_ERROR "${name} did not report ${diagnostic}:\n${output}")
    endif()
    if(NOT output MATCHES
       "compile_failure<fastregex::compile_error::${diagnostic}, ${offset}")
        message(FATAL_ERROR "${name} did not report offset ${offset}:\n${output}")
    endif()
endfunction()

fastregex_expect_compile_failure(invalid_pattern invalid_pattern.cpp invalid_pattern 2)
fastregex_expect_compile_failure(
    pattern_bytes_exceeded pattern_bytes_exceeded.cpp pattern_bytes_exceeded 0)
