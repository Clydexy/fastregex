#pragma once

#include <cstddef>
#include <cstdint>

namespace fastregex::compiler {

enum class error_code : std::uint8_t {
    none,
    invalid_pattern,
    pattern_bytes_exceeded,
    nesting_exceeded,
    ast_nodes_exceeded,
    nfa_states_exceeded,
    nfa_edges_exceeded,
    dfa_states_exceeded,
    transitions_exceeded,
    work_exceeded
};

struct diagnostic {
    error_code code = error_code::none;
    std::size_t offset = 0;
    constexpr bool operator==(const diagnostic&) const = default;
};

} // namespace fastregex::compiler
