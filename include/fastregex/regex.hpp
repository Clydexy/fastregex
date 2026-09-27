#pragma once

#include "compiler/dfa_match.hpp"
#include "compiler/parser.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace fastregex {

template <std::size_t Size> struct fixed_string {
    static_assert(Size > 0);

    char value[Size]{};

    constexpr fixed_string() = default;

    consteval fixed_string(const char (&text)[Size]) {
        for (std::size_t index = 0; index < Size; ++index) {
            value[index] = text[index];
        }
    }

    constexpr std::size_t size() const {
        return Size - 1;
    }

    constexpr std::string_view view() const {
        return {value, size()};
    }

    constexpr bool operator==(const fixed_string&) const = default;
};

template <std::size_t Size> fixed_string(const char (&)[Size]) -> fixed_string<Size>;

enum class compile_error : std::uint8_t {
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

namespace detail {

template <compile_error Code, std::size_t Offset> struct compile_failure {
    static constexpr compile_error code = Code;
    static constexpr std::size_t offset = Offset;
};

template <typename> inline constexpr bool dependent_false = false;

constexpr compile_error public_error(compiler::error_code code) {
    switch (code) {
    case compiler::error_code::none:
        return compile_error::none;
    case compiler::error_code::invalid_pattern:
        return compile_error::invalid_pattern;
    case compiler::error_code::pattern_bytes_exceeded:
        return compile_error::pattern_bytes_exceeded;
    case compiler::error_code::nesting_exceeded:
        return compile_error::nesting_exceeded;
    case compiler::error_code::ast_nodes_exceeded:
        return compile_error::ast_nodes_exceeded;
    case compiler::error_code::nfa_states_exceeded:
        return compile_error::nfa_states_exceeded;
    case compiler::error_code::nfa_edges_exceeded:
        return compile_error::nfa_edges_exceeded;
    case compiler::error_code::dfa_states_exceeded:
        return compile_error::dfa_states_exceeded;
    case compiler::error_code::transitions_exceeded:
        return compile_error::transitions_exceeded;
    case compiler::error_code::work_exceeded:
        return compile_error::work_exceeded;
    }
}

template <compile_error Code, std::size_t Offset, std::size_t> consteval void fail_compilation() {
    using failure = compile_failure<Code, Offset>;
    if constexpr (Code == compile_error::invalid_pattern) {
        static_assert(dependent_false<failure>, "fastregex: invalid_pattern");
    } else if constexpr (Code == compile_error::pattern_bytes_exceeded) {
        static_assert(dependent_false<failure>, "fastregex: pattern_bytes_exceeded");
    } else if constexpr (Code == compile_error::nesting_exceeded) {
        static_assert(dependent_false<failure>, "fastregex: nesting_exceeded");
    } else if constexpr (Code == compile_error::ast_nodes_exceeded) {
        static_assert(dependent_false<failure>, "fastregex: ast_nodes_exceeded");
    } else if constexpr (Code == compile_error::nfa_states_exceeded) {
        static_assert(dependent_false<failure>, "fastregex: nfa_states_exceeded");
    } else if constexpr (Code == compile_error::nfa_edges_exceeded) {
        static_assert(dependent_false<failure>, "fastregex: nfa_edges_exceeded");
    } else if constexpr (Code == compile_error::dfa_states_exceeded) {
        static_assert(dependent_false<failure>, "fastregex: dfa_states_exceeded");
    } else if constexpr (Code == compile_error::transitions_exceeded) {
        static_assert(dependent_false<failure>, "fastregex: transitions_exceeded");
    } else if constexpr (Code == compile_error::work_exceeded) {
        static_assert(dependent_false<failure>, "fastregex: work_exceeded");
    } else {
        static_assert(dependent_false<failure>, "fastregex: unknown compilation error");
    }
}

constexpr std::size_t ast_capacity(std::size_t pattern_size) {
    constexpr auto maximum = compiler::parser_limits{}.ast_nodes;
    if (pattern_size >= (maximum - 1) / 2) {
        return maximum;
    }
    return pattern_size * 2 + 1;
}

template <fixed_string Pattern> consteval auto parse_pattern() {
    constexpr auto parser_defaults = compiler::parser_limits{};
    constexpr compiler::parser_limits parse_limits{
        .pattern_bytes = parser_defaults.pattern_bytes,
        .nesting = parser_defaults.nesting,
        .ast_nodes = ast_capacity(Pattern.size()),
        .work = parser_defaults.work,
    };
    return compiler::parse<parse_limits>(Pattern.view());
}

template <fixed_string Pattern> inline constexpr auto parsed_pattern = parse_pattern<Pattern>();

template <fixed_string Pattern> consteval auto lower_pattern() {
    constexpr auto parsed = parsed_pattern<Pattern>;
    constexpr auto nfa_defaults = compiler::nfa_limits{};
    constexpr compiler::nfa_limits nfa_limits{
        .states = parsed.tree.size * 2,
        .edges = parsed.tree.size * 4,
        .work = nfa_defaults.work,
    };
    return compiler::lower_nfa<nfa_limits>(parsed.tree);
}

template <fixed_string Pattern> inline constexpr auto lowered_pattern = lower_pattern<Pattern>();

template <fixed_string Pattern> consteval auto count_pattern_states() {
    return compiler::count_dfa_states(lowered_pattern<Pattern>.graph);
}

template <fixed_string Pattern>
inline constexpr auto counted_pattern = count_pattern_states<Pattern>();

template <fixed_string Pattern> consteval auto compile_pattern() {
    constexpr auto parser_defaults = compiler::parser_limits{};
    constexpr auto dfa_defaults = compiler::dfa_limits{};
    if constexpr (Pattern.size() > parser_defaults.pattern_bytes) {
        fail_compilation<compile_error::pattern_bytes_exceeded, 0, Pattern.size()>();
        return compiler::dfa<1>{};
    } else if constexpr (!parsed_pattern<Pattern>) {
        constexpr auto error = parsed_pattern<Pattern>.error;
        fail_compilation<public_error(error.code), error.offset, Pattern.size()>();
        return compiler::dfa<1>{};
    } else if constexpr (!lowered_pattern<Pattern>) {
        constexpr auto error = lowered_pattern<Pattern>.error;
        fail_compilation<public_error(error.code), error.offset, Pattern.size()>();
        return compiler::dfa<1>{};
    } else if constexpr (!counted_pattern<Pattern>) {
        constexpr auto error = counted_pattern<Pattern>.error;
        fail_compilation<public_error(error.code), error.offset, Pattern.size()>();
        return compiler::dfa<1>{};
    } else {
        constexpr auto counted = counted_pattern<Pattern>;
        constexpr compiler::dfa_limits exact_limits{
            .states = counted.machine.state_count,
            .transitions = counted.machine.transition_count,
            .work = dfa_defaults.work,
        };
        constexpr auto compiled =
            compiler::determinize<exact_limits>(lowered_pattern<Pattern>.graph);
        if constexpr (!compiled) {
            fail_compilation<public_error(compiled.error.code), compiled.error.offset,
                             Pattern.size()>();
            return compiler::dfa<1>{};
        } else {
            return compiled.machine;
        }
    }
}

template <fixed_string Pattern> inline constexpr auto compiled_pattern = compile_pattern<Pattern>();

} // namespace detail

template <fixed_string Pattern> constexpr bool full_match(std::string_view subject) noexcept {
    return compiler::full_match(detail::compiled_pattern<Pattern>, subject);
}

template <fixed_string Pattern>
constexpr bool full_match(std::span<const std::uint8_t> subject) noexcept {
    return compiler::full_match(detail::compiled_pattern<Pattern>, subject);
}

template <fixed_string Pattern>
constexpr bool full_match(const std::uint8_t* subject, std::size_t length) noexcept {
    return compiler::full_match(detail::compiled_pattern<Pattern>, subject, length);
}

} // namespace fastregex
