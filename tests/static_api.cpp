#include <fastregex/regex.hpp>

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

using namespace std::string_view_literals;

constexpr bool shared_results() {
    constexpr std::array<std::uint8_t, 3> bytes{0, 'a', 0xFF};
    constexpr std::array<char, 3> characters{0, 'a', std::bit_cast<char>(std::uint8_t{0xFF})};
    return fastregex::full_match<"">("") && !fastregex::full_match<"">("a") &&
           fastregex::full_match<"a(b|c)*">("abcbcc") && !fastregex::full_match<"a(b|c)*">("abx") &&
           fastregex::full_match<"[a-c]*">("abccba") && !fastregex::full_match<"[a-c]*">("abcd") &&
           fastregex::full_match<R"(\x00a\xFF)">(std::span{bytes}) &&
           fastregex::full_match<R"(\x00a\xFF)">(
               std::string_view{characters.data(), characters.size()}) &&
           !fastregex::full_match<R"(\x00a\xFF)">(bytes.data(), 2);
}

constexpr bool all_byte_values() {
    for (unsigned value = 0; value < 256; ++value) {
        const std::array byte{static_cast<std::uint8_t>(value)};
        if (!fastregex::full_match<R"([\x00-\xFF])">(std::span{byte})) {
            return false;
        }
    }
    return true;
}

bool artifact_agreement() {
    using namespace fastregex::compiler;
    constexpr parser_limits parse_limits{.ast_nodes = 64};
    constexpr nfa_limits nfa_limits{.states = 128, .edges = 256};
    constexpr dfa_limits dfa_limits{.states = 32, .transitions = 8'192, .work = 1'000'000};
    constexpr std::string_view pattern = "a(b|[c-e])*";

    const auto parsed = parse<parse_limits>(pattern);
    const auto lowered = lower_nfa<nfa_limits>(parsed.tree);
    const auto runtime = determinize<dfa_limits>(lowered.graph);
    constexpr const auto& constant = fastregex::detail::compiled_pattern<"a(b|[c-e])*">;
    if (!parsed || !lowered || !runtime || runtime.machine.state_count != constant.state_count ||
        runtime.machine.transition_count != constant.transition_count ||
        runtime.machine.byte_class_count != constant.byte_class_count ||
        runtime.machine.byte_classes != constant.byte_classes ||
        runtime.machine.start != constant.start || runtime.machine.sink != constant.sink) {
        return false;
    }
    for (std::size_t state = 0; state < constant.state_count; ++state) {
        if (runtime.machine.states[state] != constant.states[state]) {
            return false;
        }
    }
    return true;
}

static_assert(shared_results());
static_assert(all_byte_values());
static_assert(fastregex::detail::compiled_pattern<"a">.state_count == 3);
static_assert(fastregex::detail::compiled_pattern<"a">.states.size() == 3);

bool runtime_cases() {
    return shared_results() && all_byte_values() && artifact_agreement();
}

} // namespace

int main() {
    const std::array tests{std::pair{"runtime and constant results", &runtime_cases}};
    for (const auto& [name, run] : tests) {
        if (!run()) {
            std::cerr << "FAILED: " << name << '\n';
            return 1;
        }
    }
    return 0;
}
