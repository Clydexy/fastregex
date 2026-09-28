#include <fastregex/regex.hpp>

#include "compiler/dfa_match.hpp"
#include "compiler/diagnostic.hpp"
#include "compiler/nfa.hpp"
#include "compiler/parser.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using fastregex::compiler::diagnostic;
using fastregex::compiler::error_code;
using namespace std::string_view_literals;

struct match_case {
    std::string_view text;
    bool expected;
};

constexpr std::array<char, 2> nul_ff{0, std::bit_cast<char>(std::uint8_t{0xFF})};
constexpr std::array<char, 2> boundary_match{0, std::bit_cast<char>(std::uint8_t{0x7F})};
constexpr std::array<char, 2> boundary_miss{0, std::bit_cast<char>(std::uint8_t{0x7E})};

consteval auto make_all_bytes() {
    std::array<char, 256> bytes{};
    for (std::size_t value = 0; value < bytes.size(); ++value) {
        bytes[value] = std::bit_cast<char>(static_cast<std::uint8_t>(value));
    }
    return bytes;
}

constexpr auto all_bytes = make_all_bytes();

constexpr std::array concat_cases{match_case{"ab", true}, match_case{"", false},
                                  match_case{"a", false}, match_case{"abc", false}};
constexpr std::array alternate_cases{match_case{"a", true}, match_case{"bc", true},
                                     match_case{"", false}, match_case{"b", false},
                                     match_case{"abc", false}};
constexpr std::array star_cases{match_case{"a", true},    match_case{"ab", true},
                                match_case{"abbb", true}, match_case{"", false},
                                match_case{"b", false},   match_case{"aba", false}};
constexpr std::array group_cases{match_case{"a", true},   match_case{"ab", true},
                                 match_case{"ac", true},  match_case{"abcbcc", true},
                                 match_case{"", false},   match_case{"b", false},
                                 match_case{"abx", false}};
constexpr std::array class_cases{match_case{"az", true},
                                 match_case{"b\0"sv, true},
                                 match_case{std::string_view{nul_ff.data() + 1, 1}, false},
                                 match_case{"ax", false},
                                 match_case{"dx", false},
                                 match_case{"a", false}};
constexpr std::array complement_cases{match_case{"b", true}, match_case{"\0"sv, true},
                                      match_case{std::string_view{nul_ff.data() + 1, 1}, true},
                                      match_case{"a", false}, match_case{"", false}};
constexpr std::array precedence_cases{match_case{"ab", true},  match_case{"c", true},
                                      match_case{"cd", true},  match_case{"cddd", true},
                                      match_case{"", false},   match_case{"abc", false},
                                      match_case{"acd", false}};
constexpr std::array nullable_cycle_cases{match_case{"", true}, match_case{"a", true},
                                          match_case{"aaaa", true}, match_case{"b", false},
                                          match_case{"ab", false}};
constexpr std::array boundary_cases{
    match_case{std::string_view{nul_ff.data(), nul_ff.size()}, true},
    match_case{std::string_view{boundary_match.data(), boundary_match.size()}, true},
    match_case{std::string_view{boundary_miss.data(), boundary_miss.size()}, false},
    match_case{"\0"sv, false}, match_case{std::string_view{nul_ff.data() + 1, 1}, false}};
constexpr std::array alphabet_cases{
    match_case{"", true}, match_case{std::string_view{all_bytes.data(), all_bytes.size()}, true}};

std::string escaped(std::string_view bytes) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    for (const char character : bytes) {
        const auto byte = std::bit_cast<std::uint8_t>(character);
        if (byte >= 0x20 && byte <= 0x7E && byte != '\\' && byte != '"') {
            result.push_back(character);
        } else {
            result += "\\x";
            result.push_back(digits[byte >> 4]);
            result.push_back(digits[byte & 0x0F]);
        }
    }
    return result;
}

const char* error_name(error_code code) {
    switch (code) {
    case error_code::none:
        return "none";
    case error_code::invalid_pattern:
        return "invalid_pattern";
    case error_code::pattern_bytes_exceeded:
        return "pattern_bytes_exceeded";
    case error_code::nesting_exceeded:
        return "nesting_exceeded";
    case error_code::ast_nodes_exceeded:
        return "ast_nodes_exceeded";
    case error_code::nfa_states_exceeded:
        return "nfa_states_exceeded";
    case error_code::nfa_edges_exceeded:
        return "nfa_edges_exceeded";
    case error_code::dfa_states_exceeded:
        return "dfa_states_exceeded";
    case error_code::transitions_exceeded:
        return "transitions_exceeded";
    case error_code::work_exceeded:
        return "work_exceeded";
    }
    return "unknown";
}

template <fastregex::fixed_string Pattern, std::size_t Count>
constexpr bool constant_group(const std::array<match_case, Count>& cases) {
    for (const auto& fixture : cases) {
        if (fastregex::full_match<Pattern>(fixture.text) != fixture.expected) {
            return false;
        }
    }
    return true;
}

template <fastregex::fixed_string Pattern, std::size_t Count>
bool runtime_group(const std::array<match_case, Count>& cases) {
    using namespace fastregex::compiler;
    constexpr parser_limits parse_limits{.ast_nodes = 256};
    constexpr nfa_limits lower_limits{.states = 512, .edges = 1024};
    constexpr dfa_limits determinize_limits{
        .states = 128, .transitions = 32'768, .work = 16'777'216};

    const auto parsed = parse<parse_limits>(Pattern.view());
    if (!parsed) {
        std::cerr << "FAILED: phase=parse pattern=\"" << escaped(Pattern.view())
                  << "\" expected=none actual=" << error_name(parsed.error.code)
                  << " offset=" << parsed.error.offset << '\n';
        return false;
    }
    const auto lowered = lower_nfa<lower_limits>(parsed.tree);
    if (!lowered) {
        std::cerr << "FAILED: phase=nfa pattern=\"" << escaped(Pattern.view())
                  << "\" expected=none actual=" << error_name(lowered.error.code) << '\n';
        return false;
    }
    const auto compiled = determinize<determinize_limits>(lowered.graph);
    if (!compiled) {
        std::cerr << "FAILED: phase=dfa pattern=\"" << escaped(Pattern.view())
                  << "\" expected=none actual=" << error_name(compiled.error.code) << '\n';
        return false;
    }

    for (const auto& fixture : cases) {
        const bool public_actual = fastregex::full_match<Pattern>(fixture.text);
        const bool runtime_actual = full_match(compiled.machine, fixture.text);
        if (public_actual != fixture.expected || runtime_actual != fixture.expected) {
            std::cerr << "FAILED: pattern=\"" << escaped(Pattern.view()) << "\" text=\""
                      << escaped(fixture.text) << "\" expected=" << fixture.expected
                      << " public=" << public_actual << " runtime=" << runtime_actual << '\n';
            return false;
        }
    }
    return true;
}

constexpr bool constant_corpus() {
    return constant_group<"ab">(concat_cases) && constant_group<"a|bc">(alternate_cases) &&
           constant_group<"ab*">(star_cases) && constant_group<"a(b|c)*">(group_cases) &&
           constant_group<"[a-c][^x]">(class_cases) && constant_group<"[^a]">(complement_cases) &&
           constant_group<"ab|cd*">(precedence_cases) &&
           constant_group<"(|a*)*">(nullable_cycle_cases) &&
           constant_group<R"(\x00[\x7F-\xFF])">(boundary_cases) &&
           constant_group<R"([\x00-\xFF]*)">(alphabet_cases);
}

bool runtime_corpus() {
    return runtime_group<"ab">(concat_cases) && runtime_group<"a|bc">(alternate_cases) &&
           runtime_group<"ab*">(star_cases) && runtime_group<"a(b|c)*">(group_cases) &&
           runtime_group<"[a-c][^x]">(class_cases) && runtime_group<"[^a]">(complement_cases) &&
           runtime_group<"ab|cd*">(precedence_cases) &&
           runtime_group<"(|a*)*">(nullable_cycle_cases) &&
           runtime_group<R"(\x00[\x7F-\xFF])">(boundary_cases) &&
           runtime_group<R"([\x00-\xFF]*)">(alphabet_cases);
}

struct malformed_case {
    std::string_view pattern;
    std::size_t offset;
};

constexpr std::array malformed_cases{malformed_case{"a**", 2},   malformed_case{"[z-a]", 3},
                                     malformed_case{R"(\n)", 1}, malformed_case{"(", 1},
                                     malformed_case{"[]", 1},    malformed_case{"a+", 1}};

constexpr bool constant_malformed_cases() {
    constexpr fastregex::compiler::parser_limits limits{.ast_nodes = 64};
    for (const auto& fixture : malformed_cases) {
        const auto parsed = fastregex::compiler::parse<limits>(fixture.pattern);
        if (parsed || parsed.error != diagnostic{error_code::invalid_pattern, fixture.offset}) {
            return false;
        }
    }
    return true;
}

bool runtime_malformed_cases() {
    constexpr fastregex::compiler::parser_limits limits{.ast_nodes = 64};
    for (const auto& fixture : malformed_cases) {
        const auto parsed = fastregex::compiler::parse<limits>(fixture.pattern);
        const diagnostic expected{error_code::invalid_pattern, fixture.offset};
        if (parsed || parsed.error != expected) {
            std::cerr << "FAILED: phase=parse pattern=\"" << escaped(fixture.pattern)
                      << "\" expected=invalid_pattern@" << expected.offset
                      << " actual=" << error_name(parsed.error.code) << '@' << parsed.error.offset
                      << '\n';
            return false;
        }
    }
    return true;
}

bool expect_diagnostic(std::string_view phase, std::string_view pattern, diagnostic actual,
                       error_code expected) {
    if (actual == diagnostic{expected, 0}) {
        return true;
    }
    std::cerr << "FAILED: phase=" << phase << " pattern=\"" << escaped(pattern)
              << "\" expected=" << error_name(expected) << "@0 actual=" << error_name(actual.code)
              << '@' << actual.offset << '\n';
    return false;
}

bool resource_limits() {
    using namespace fastregex::compiler;
    constexpr parser_limits pattern_limit{.pattern_bytes = 0, .ast_nodes = 8};
    constexpr parser_limits nesting_limit{.nesting = 0, .ast_nodes = 8};
    constexpr parser_limits node_limit{.ast_nodes = 0};
    constexpr parser_limits parser_work_limit{.ast_nodes = 8, .work = 0};

    const auto pattern_failure = parse<pattern_limit>("a");
    const auto nesting_failure = parse<nesting_limit>("()");
    const auto node_failure = parse<node_limit>("");
    const auto parser_work_failure = parse<parser_work_limit>("");

    constexpr parser_limits parse_limits{.ast_nodes = 8};
    const auto parsed = parse<parse_limits>("a");
    constexpr nfa_limits state_limit{.states = 1, .edges = 1};
    constexpr nfa_limits edge_limit{.states = 2, .edges = 0};
    constexpr nfa_limits nfa_work_limit{.states = 2, .edges = 1, .work = 0};
    const auto state_failure = lower_nfa<state_limit>(parsed.tree);
    const auto edge_failure = lower_nfa<edge_limit>(parsed.tree);
    const auto nfa_work_failure = lower_nfa<nfa_work_limit>(parsed.tree);

    constexpr nfa_limits lower_limits{.states = 2, .edges = 1};
    const auto lowered = lower_nfa<lower_limits>(parsed.tree);
    constexpr dfa_limits dfa_state_limit{.states = 2, .transitions = 768, .work = 100'000};
    constexpr dfa_limits transition_limit{.states = 3, .transitions = 767, .work = 100'000};
    constexpr dfa_limits dfa_work_limit{.states = 3, .transitions = 768, .work = 0};
    const auto dfa_state_failure = determinize<dfa_state_limit>(lowered.graph);
    const auto transition_failure = determinize<transition_limit>(lowered.graph);
    const auto dfa_work_failure = determinize<dfa_work_limit>(lowered.graph);

    return expect_diagnostic("parse", "a", pattern_failure.error,
                             error_code::pattern_bytes_exceeded) &&
           expect_diagnostic("parse", "()", nesting_failure.error, error_code::nesting_exceeded) &&
           expect_diagnostic("parse", "", node_failure.error, error_code::ast_nodes_exceeded) &&
           expect_diagnostic("parse", "", parser_work_failure.error, error_code::work_exceeded) &&
           expect_diagnostic("nfa", "a", state_failure.error, error_code::nfa_states_exceeded) &&
           expect_diagnostic("nfa", "a", edge_failure.error, error_code::nfa_edges_exceeded) &&
           expect_diagnostic("nfa", "a", nfa_work_failure.error, error_code::work_exceeded) &&
           expect_diagnostic("dfa", "a", dfa_state_failure.error,
                             error_code::dfa_states_exceeded) &&
           expect_diagnostic("dfa", "a", transition_failure.error,
                             error_code::transitions_exceeded) &&
           expect_diagnostic("dfa", "a", dfa_work_failure.error, error_code::work_exceeded);
}

static_assert(constant_corpus());
static_assert(constant_malformed_cases());

} // namespace

int main() {
    if (!runtime_corpus() || !runtime_malformed_cases() || !resource_limits()) {
        return 1;
    }
    return 0;
}
