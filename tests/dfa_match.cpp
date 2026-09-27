#include "compiler/dfa_match.hpp"
#include "compiler/parser.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>

namespace {

using namespace fastregex::compiler;
using namespace std::string_view_literals;

constexpr parser_limits parser_test_limits{.ast_nodes = 64};
constexpr nfa_limits nfa_test_limits{.states = 128, .edges = 256};
constexpr dfa_limits dfa_test_limits{.states = 32, .transitions = 8'192, .work = 1'000'000};

constexpr auto compile(std::string_view pattern) {
    const auto parsed = parse<parser_test_limits>(pattern);
    const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
    return determinize<dfa_test_limits>(lowered.graph);
}

struct match_case {
    std::string_view pattern;
    std::string_view accepted;
    std::string_view rejected;
};

constexpr std::array match_cases{
    match_case{"", ""sv, "a"sv},
    match_case{"a", "a"sv, ""sv},
    match_case{"ab", "ab"sv, "a"sv},
    match_case{"a|bc", "bc"sv, "abc"sv},
    match_case{"(ab)*", "abab"sv, "aba"sv},
    match_case{"ab*", "abbb"sv, "ba"sv},
    match_case{"[a-c]", "b"sv, "d"sv},
    match_case{"[^a]", "\0"sv, "a"sv},
    match_case{R"(\x00\xFF)", "\0\xFF"sv, "\xFF"sv},
    match_case{"a|", ""sv, "aa"sv},
    match_case{"(|a*)*", "aaaa"sv, "b"sv},
};

constexpr bool operator_cases() {
    for (const auto& fixture : match_cases) {
        const auto compiled = compile(fixture.pattern);
        if (!compiled || !full_match(compiled.machine, fixture.accepted) ||
            full_match(compiled.machine, fixture.rejected)) {
            return false;
        }
    }
    return true;
}

constexpr bool explicit_lengths() {
    const auto compiled = compile("ab");
    constexpr std::array<std::uint8_t, 3> subject{'a', 'b', 'x'};
    return compiled &&
           full_match(compiled.machine, std::span<const std::uint8_t>{subject.data(), 2}) &&
           !full_match(compiled.machine, std::span<const std::uint8_t>{subject.data(), 1}) &&
           !full_match(compiled.machine, std::span<const std::uint8_t>{subject.data(), 3});
}

constexpr bool embedded_nul_and_high_byte() {
    const auto compiled = compile(R"(\x00a\xFF)");
    constexpr std::array<std::uint8_t, 3> bytes{0, 'a', 0xFF};
    constexpr std::array<char, 3> characters{0, 'a', std::bit_cast<char>(std::uint8_t{0xFF})};
    return compiled && full_match(compiled.machine, std::span{bytes}) &&
           full_match(compiled.machine, std::string_view{characters.data(), characters.size()}) &&
           !full_match(compiled.machine, std::span<const std::uint8_t>{bytes.data(), 2});
}

constexpr bool zero_length_does_not_read() {
    const auto empty = compile("");
    const auto literal = compile("a");
    std::size_t reads = 0;
    const bool matched = detail::full_match_impl(empty.machine, 0, [&reads](std::size_t) {
        ++reads;
        return std::uint8_t{0};
    });
    return empty && literal && matched && reads == 0 &&
           full_match(empty.machine, std::string_view{}) &&
           full_match(empty.machine, std::span<const std::uint8_t>{}) &&
           full_match(empty.machine, static_cast<const std::uint8_t*>(nullptr), 0) &&
           !full_match(literal.machine, static_cast<const std::uint8_t*>(nullptr), 0) &&
           !full_match(empty.machine, static_cast<const std::uint8_t*>(nullptr), 1);
}

bool all_byte_values() {
    const auto one_byte = compile(R"([\x00-\xFF])");
    const auto all_bytes = compile(R"([\x00-\xFF]*)");
    std::array<std::uint8_t, 256> bytes{};
    std::array<char, 256> characters{};
    for (std::size_t value = 0; value < bytes.size(); ++value) {
        bytes[value] = static_cast<std::uint8_t>(value);
        characters[value] = std::bit_cast<char>(bytes[value]);
        if (!full_match(one_byte.machine, std::span<const std::uint8_t>{bytes.data() + value, 1})) {
            return false;
        }
    }
    return one_byte && all_bytes &&
           full_match(all_bytes.machine, std::span<const std::uint8_t>{bytes}) &&
           full_match(all_bytes.machine, std::string_view{characters.data(), characters.size()}) &&
           !full_match(one_byte.machine, std::span<const std::uint8_t>{bytes});
}

bool sink_stops_input_reads() {
    const auto compiled = compile("a");
    constexpr std::array<std::uint8_t, 4> subject{'b', 'a', 'a', 'a'};
    std::size_t reads = 0;
    const bool matched = detail::full_match_impl(compiled.machine, subject.size(),
                                                 [&reads, &subject](std::size_t offset) {
                                                     ++reads;
                                                     return subject[offset];
                                                 });
    return compiled && !matched && reads == 1;
}

constexpr bool malformed_machine_is_rejected() {
    dfa<2> machine;
    machine.state_count = 2;
    machine.transition_count = 512;
    machine.start = 1;
    machine.sink = 0;
    machine.states[1].transitions.fill(no_dfa_state);
    return !full_match(machine, "a");
}

static_assert(explicit_lengths());
static_assert(embedded_nul_and_high_byte());
static_assert(zero_length_does_not_read());
static_assert(malformed_machine_is_rejected());
static_assert(noexcept(full_match(std::declval<const dfa<dfa_test_limits.states>&>(),
                                  std::declval<std::string_view>())));

bool runtime_cases() {
    return operator_cases() && explicit_lengths() && embedded_nul_and_high_byte() &&
           zero_length_does_not_read() && all_byte_values() && sink_stops_input_reads() &&
           malformed_machine_is_rejected();
}

} // namespace

int main() {
    const std::array tests{std::pair{"runtime cases", &runtime_cases}};
    for (const auto& [name, run] : tests) {
        if (!run()) {
            std::cerr << "FAILED: " << name << '\n';
            return 1;
        }
    }
    return 0;
}
