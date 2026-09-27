#include "compiler/dfa_dump.hpp"
#include "compiler/parser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>

namespace {

using namespace fastregex::compiler;
using namespace std::string_view_literals;

constexpr parser_limits parser_test_limits{.ast_nodes = 64};
constexpr nfa_limits nfa_test_limits{.states = 128, .edges = 256};
constexpr dfa_limits dfa_test_limits{.states = 32, .transitions = 8'192, .work = 1'000'000};

template <std::size_t StateCapacity>
constexpr bool accepts(const dfa<StateCapacity>& machine, std::string_view subject) {
    if (machine.start >= machine.state_count) {
        return false;
    }
    auto state = machine.start;
    for (const char character : subject) {
        const auto byte = static_cast<std::uint8_t>(static_cast<unsigned char>(character));
        state = machine.states[state].transitions[byte];
    }
    return machine.states[state].accepting;
}

template <std::size_t StateCapacity>
constexpr bool is_complete_and_reachable(const dfa<StateCapacity>& machine) {
    if (machine.state_count == 0 || machine.start >= machine.state_count ||
        machine.sink >= machine.state_count ||
        machine.transition_count != machine.state_count * byte_alphabet_size) {
        return false;
    }
    for (std::size_t state = 0; state < machine.state_count; ++state) {
        for (const auto target : machine.states[state].transitions) {
            if (target >= machine.state_count) {
                return false;
            }
        }
    }

    std::array<bool, StateCapacity> reached{};
    std::array<dfa_state_id, StateCapacity> worklist{};
    std::size_t head = 0;
    std::size_t tail = 0;
    reached[machine.start] = true;
    worklist[tail++] = machine.start;
    while (head < tail) {
        const auto state = worklist[head++];
        for (const auto target : machine.states[state].transitions) {
            if (!reached[target]) {
                reached[target] = true;
                worklist[tail++] = target;
            }
        }
    }
    for (std::size_t state = 0; state < machine.state_count; ++state) {
        if (state != machine.sink && !reached[state]) {
            return false;
        }
    }
    return true;
}

template <dfa_limits Limits = dfa_test_limits> constexpr auto compile(std::string_view pattern) {
    const auto parsed = parse<parser_test_limits>(pattern);
    const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
    return determinize<Limits>(lowered.graph);
}

struct language_case {
    std::string_view pattern;
    std::string_view accepted;
    std::string_view rejected;
};

constexpr std::array language_cases{
    language_case{"", ""sv, "a"sv},
    language_case{"a", "a"sv, ""sv},
    language_case{"ab", "ab"sv, "a"sv},
    language_case{"a|bc", "bc"sv, "abc"sv},
    language_case{"(ab)*", "abab"sv, "aba"sv},
    language_case{"[a-c]", "c"sv, "d"sv},
    language_case{"[^a]", "\0"sv, "a"sv},
    language_case{R"(\x00\xFF)", "\0\xFF"sv, "\xFF"sv},
    language_case{"a|", ""sv, "aa"sv},
    language_case{"(|a*)*", "aaaa"sv, "b"sv},
};

constexpr bool languages() {
    for (const auto& fixture : language_cases) {
        const auto compiled = compile(fixture.pattern);
        if (!compiled || !is_complete_and_reachable(compiled.machine) ||
            !accepts(compiled.machine, fixture.accepted) ||
            accepts(compiled.machine, fixture.rejected)) {
            return false;
        }
    }
    return true;
}

constexpr bool literal_states() {
    const auto compiled = compile("a");
    if (!compiled || compiled.machine.state_count != 3 || compiled.machine.start != 1 ||
        compiled.machine.sink != 0 || compiled.machine.states[0].accepting ||
        compiled.machine.states[1].accepting || !compiled.machine.states[2].accepting) {
        return false;
    }
    for (std::size_t byte = 0; byte < byte_alphabet_size; ++byte) {
        const auto expected = byte == static_cast<unsigned>('a') ? 2 : 0;
        if (compiled.machine.states[0].transitions[byte] != 0 ||
            compiled.machine.states[1].transitions[byte] != expected ||
            compiled.machine.states[2].transitions[byte] != 0) {
            return false;
        }
    }
    return true;
}

constexpr bool epsilon_states() {
    const auto compiled = compile("");
    if (!compiled || compiled.machine.state_count != 2 || compiled.machine.start != 1 ||
        !compiled.machine.states[1].accepting) {
        return false;
    }
    for (const auto target : compiled.machine.states[1].transitions) {
        if (target != compiled.machine.sink) {
            return false;
        }
    }
    return true;
}

constexpr bool alternate_states() {
    const auto compiled = compile("a|b");
    return compiled && compiled.machine.state_count == 4 && compiled.machine.start == 1 &&
           compiled.machine.states[1].transitions['a'] == 2 &&
           compiled.machine.states[1].transitions['b'] == 3 &&
           compiled.machine.states[1].transitions['c'] == 0 &&
           compiled.machine.states[2].accepting && compiled.machine.states[3].accepting;
}

constexpr bool star_states() {
    const auto compiled = compile("a*");
    return compiled && compiled.machine.state_count == 3 && compiled.machine.start == 1 &&
           compiled.machine.states[1].accepting && compiled.machine.states[2].accepting &&
           compiled.machine.states[1].transitions['a'] == 2 &&
           compiled.machine.states[2].transitions['a'] == 2 &&
           compiled.machine.states[1].transitions['b'] == 0 &&
           compiled.machine.states[2].transitions['b'] == 0;
}

constexpr bool stable_numbering() {
    const auto first = compile("a(b|[c-e])*");
    const auto second = compile("a(b|[c-e])*");
    return first && second && first.machine == second.machine;
}

constexpr bool count_pass_agrees() {
    const auto parsed = parse<parser_test_limits>("a");
    const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
    const auto counted = count_dfa_states<dfa_test_limits>(lowered.graph);
    const auto compiled = determinize<dfa_test_limits>(lowered.graph);
    return counted && compiled && counted.error == compiled.error &&
           counted.work_used == compiled.work_used &&
           counted.machine.state_count == compiled.machine.state_count &&
           counted.machine.transition_count == compiled.machine.transition_count &&
           counted.machine.start == compiled.machine.start &&
           counted.machine.sink == compiled.machine.sink;
}

bool default_limits_smoke() {
    const auto parsed = parse<parser_test_limits>("a");
    const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
    const auto compiled = determinize(lowered.graph);
    return compiled && compiled.machine.state_count == 3 && accepts(compiled.machine, "a") &&
           !accepts(compiled.machine, "aa");
}

template <dfa_limits Limits> constexpr bool fails_cleanly(error_code code) {
    const auto compiled = compile<Limits>("a");
    return !compiled && compiled.error == diagnostic{code, 0} &&
           compiled.machine.state_count == 0 && compiled.machine.transition_count == 0 &&
           compiled.machine.start == no_dfa_state && compiled.machine.sink == no_dfa_state;
}

constexpr bool limits() {
    constexpr dfa_limits exact{.states = 3, .transitions = 768, .work = 100'000};
    constexpr dfa_limits short_states{.states = 2, .transitions = 768, .work = 100'000};
    constexpr dfa_limits short_transitions{.states = 3, .transitions = 767, .work = 100'000};
    constexpr dfa_limits no_work{.states = 3, .transitions = 768, .work = 0};
    const auto admitted = compile<exact>("a");
    return admitted && admitted.machine.state_count == 3 &&
           admitted.machine.transition_count == 768 &&
           fails_cleanly<short_states>(error_code::dfa_states_exceeded) &&
           fails_cleanly<short_transitions>(error_code::transitions_exceeded) &&
           fails_cleanly<no_work>(error_code::work_exceeded);
}

constexpr auto epsilon_cycle = compile("(|a*)*");
static_assert(epsilon_cycle);
static_assert(is_complete_and_reachable(epsilon_cycle.machine));
static_assert(accepts(epsilon_cycle.machine, ""));
static_assert(accepts(epsilon_cycle.machine, "aaaa"));
static_assert(!accepts(epsilon_cycle.machine, "b"));
static_assert(literal_states());
static_assert(epsilon_states());
static_assert(alternate_states());
static_assert(star_states());
static_assert(limits());
static_assert(count_pass_agrees());

bool dumps() {
    const auto compiled = compile("a");
    constexpr std::string_view text = "state 0 sink\n"
                                      "state 1 start\n"
                                      "state 2 accept\n"
                                      "transition 0 00-FF -> 0\n"
                                      "transition 1 00-60 -> 0\n"
                                      "transition 1 61 -> 2\n"
                                      "transition 1 62-FF -> 0\n"
                                      "transition 2 00-FF -> 0\n";
    constexpr std::string_view dot = "digraph dfa {\n"
                                     "  rankdir=LR;\n"
                                     "  start [shape=point];\n"
                                     "  0 [shape=circle];\n"
                                     "  1 [shape=circle];\n"
                                     "  2 [shape=doublecircle];\n"
                                     "  start -> 1;\n"
                                     "  0 -> 0 [label=\"00-FF\"];\n"
                                     "  1 -> 0 [label=\"00-60\"];\n"
                                     "  1 -> 2 [label=\"61\"];\n"
                                     "  1 -> 0 [label=\"62-FF\"];\n"
                                     "  2 -> 0 [label=\"00-FF\"];\n"
                                     "}\n";
    return compiled && dump_dfa(compiled.machine) == text &&
           dump_dfa_dot(compiled.machine) == dot && dump_dfa(compiled.machine) == text;
}

bool runtime_cases() {
    return languages() && literal_states() && epsilon_states() && alternate_states() &&
           star_states() && stable_numbering() && count_pass_agrees() && limits() &&
           default_limits_smoke();
}

} // namespace

int main() {
    const std::array tests{std::pair{"runtime cases", &runtime_cases}, std::pair{"dumps", &dumps}};
    for (const auto& [name, run] : tests) {
        if (!run()) {
            std::cerr << "FAILED: " << name << '\n';
            return 1;
        }
    }
    return 0;
}
