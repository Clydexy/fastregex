#include "compiler/dfa_equivalence.hpp"
#include "compiler/dfa_match.hpp"
#include "compiler/parser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>

namespace {

using namespace fastregex::compiler;

constexpr parser_limits parser_test_limits{.ast_nodes = 64};
constexpr nfa_limits nfa_test_limits{.states = 128, .edges = 256};
constexpr dfa_limits dfa_test_limits{.states = 32, .transitions = 8'192, .work = 1'000'000};
constexpr equivalence_limits equivalence_test_limits{.pairs = 1'024, .work = 4'000'000};

constexpr auto compile(std::string_view pattern) {
    const auto parsed = parse<parser_test_limits>(pattern);
    const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
    return determinize<dfa_test_limits>(lowered.graph);
}

template <std::size_t Capacity>
constexpr bool replays_difference(const dfa<Capacity>& left, const dfa<Capacity>& right,
                                  const auto& result) {
    const std::span subject{result.counterexample.data(), result.counterexample_size};
    return full_match(left, subject) != full_match(right, subject);
}

constexpr bool known_pairs() {
    constexpr std::array equivalent_patterns{
        std::pair{"a", "a|a"},
        std::pair{"a*", "(a*)*"},
        std::pair{"ab|ac", "a(b|c)"},
        std::pair{"[ab]", "a|b"},
    };
    for (const auto& [left_pattern, right_pattern] : equivalent_patterns) {
        const auto left = compile(left_pattern);
        const auto right = compile(right_pattern);
        const auto result = equivalent_dfas<equivalence_test_limits>(left.machine, right.machine);
        if (!left || !right || !result || !result.equivalent || result.counterexample_size != 0) {
            return false;
        }
    }

    const auto epsilon = compile("");
    const auto literal = compile("a");
    const auto empty_mismatch =
        equivalent_dfas<equivalence_test_limits>(epsilon.machine, literal.machine);
    if (!empty_mismatch || empty_mismatch.equivalent || empty_mismatch.counterexample_size != 0 ||
        !replays_difference(epsilon.machine, literal.machine, empty_mismatch)) {
        return false;
    }

    const auto a = compile("a");
    const auto b = compile("b");
    const auto byte_mismatch = equivalent_dfas<equivalence_test_limits>(a.machine, b.machine);
    if (!byte_mismatch || byte_mismatch.equivalent || byte_mismatch.counterexample_size != 1 ||
        byte_mismatch.counterexample[0] != 'a' ||
        !replays_difference(a.machine, b.machine, byte_mismatch)) {
        return false;
    }

    const auto ab = compile("ab");
    const auto ac = compile("ac");
    const auto two_byte_mismatch = equivalent_dfas<equivalence_test_limits>(ab.machine, ac.machine);
    if (!two_byte_mismatch || two_byte_mismatch.equivalent ||
        two_byte_mismatch.counterexample_size != 2 || two_byte_mismatch.counterexample[0] != 'a' ||
        two_byte_mismatch.counterexample[1] != 'b' ||
        !replays_difference(ab.machine, ac.machine, two_byte_mismatch)) {
        return false;
    }
    for (std::size_t byte = 0; byte < byte_alphabet_size; ++byte) {
        const std::array subject{static_cast<std::uint8_t>(byte)};
        if (full_match(ab.machine, std::span{subject}) !=
            full_match(ac.machine, std::span{subject})) {
            return false;
        }
    }
    return true;
}

constexpr dfa<3> compact_literal() {
    dfa<3> machine;
    machine.state_count = 3;
    machine.byte_class_count = 2;
    machine.transition_count = 6;
    machine.start = 1;
    machine.sink = 0;
    machine.byte_classes.fill(0);
    machine.byte_classes['a'] = 1;
    machine.states[0].transitions[0] = 0;
    machine.states[0].transitions[1] = 0;
    machine.states[1].transitions[0] = 0;
    machine.states[1].transitions[1] = 2;
    machine.states[2].transitions[0] = 0;
    machine.states[2].transitions[1] = 0;
    machine.states[2].accepting = true;
    return machine;
}

constexpr dfa<3> renumbered_literal() {
    dfa<3> machine;
    machine.state_count = 3;
    machine.byte_class_count = 2;
    machine.transition_count = 6;
    machine.start = 0;
    machine.sink = 2;
    machine.byte_classes.fill(0);
    machine.byte_classes['a'] = 1;
    machine.states[0].transitions[0] = 2;
    machine.states[0].transitions[1] = 1;
    machine.states[1].transitions[0] = 2;
    machine.states[1].transitions[1] = 2;
    machine.states[1].accepting = true;
    machine.states[2].transitions[0] = 2;
    machine.states[2].transitions[1] = 2;
    return machine;
}

constexpr bool numbering_and_partitions() {
    const auto dense = compile("a");
    constexpr auto compact = compact_literal();
    constexpr auto renumbered = renumbered_literal();
    const auto compact_result = equivalent_dfas<equivalence_test_limits>(dense.machine, compact);
    const auto numbered_result = equivalent_dfas<equivalence_test_limits>(compact, renumbered);
    return dense && compact_result && compact_result.equivalent && numbered_result &&
           numbered_result.equivalent;
}

constexpr dfa<3> reduced_machine(std::size_t seed) {
    dfa<3> machine;
    machine.state_count = 3;
    machine.byte_class_count = 3;
    machine.transition_count = 9;
    machine.start = 1;
    machine.sink = 0;
    machine.byte_classes.fill(0);
    machine.byte_classes['a'] = 1;
    machine.byte_classes['b'] = 2;
    machine.states[0].transitions[0] = 0;
    machine.states[0].transitions[1] = 0;
    machine.states[0].transitions[2] = 0;
    for (std::size_t state = 1; state < 3; ++state) {
        machine.states[state].transitions[0] = 0;
        for (std::size_t byte_class = 1; byte_class < 3; ++byte_class) {
            machine.states[state].transitions[byte_class] = static_cast<dfa_state_id>(seed % 3);
            seed /= 3;
        }
    }
    machine.states[1].accepting = (seed & 1U) != 0;
    machine.states[2].accepting = (seed & 2U) != 0;
    return machine;
}

constexpr bool exhaustive_equivalent(const dfa<3>& left, const dfa<3>& right) {
    std::array<std::uint8_t, 8> subject{};
    if (full_match(left, std::span<const std::uint8_t>{}) !=
        full_match(right, std::span<const std::uint8_t>{})) {
        return false;
    }
    for (std::size_t length = 1; length <= subject.size(); ++length) {
        const auto count = std::size_t{1} << length;
        for (std::size_t value = 0; value < count; ++value) {
            for (std::size_t offset = 0; offset < length; ++offset) {
                subject[offset] = ((value >> offset) & 1U) == 0 ? 'a' : 'b';
            }
            const std::span input{subject.data(), length};
            if (full_match(left, input) != full_match(right, input)) {
                return false;
            }
        }
    }
    return true;
}

bool reduced_alphabet_exhaustive() {
    for (std::size_t left_seed = 0; left_seed < 32; ++left_seed) {
        const auto left = reduced_machine(left_seed);
        for (std::size_t right_seed = 0; right_seed < 32; ++right_seed) {
            const auto right = reduced_machine(right_seed);
            const auto expected = exhaustive_equivalent(left, right);
            const auto result = equivalent_dfas<equivalence_test_limits>(left, right);
            if (!result || result.equivalent != expected ||
                (!result.equivalent && !replays_difference(left, right, result))) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool limits() {
    constexpr equivalence_limits one_pair{.pairs = 1, .work = 10'000};
    constexpr equivalence_limits no_work{.pairs = 8, .work = 0};
    const auto a = compile("a");
    const auto b = compile("b");
    const auto pair_failure = equivalent_dfas<one_pair>(a.machine, b.machine);
    const auto work_failure = equivalent_dfas<no_work>(a.machine, b.machine);
    return !pair_failure && pair_failure.error.code == error_code::work_exceeded &&
           pair_failure.counterexample_size == 0 && !work_failure &&
           work_failure.error.code == error_code::work_exceeded;
}

static_assert(numbering_and_partitions());
static_assert(limits());

} // namespace

int main() {
    const std::array tests{
        std::pair{"known pairs", &known_pairs},
        std::pair{"numbering and partitions", &numbering_and_partitions},
        std::pair{"reduced alphabet exhaustive", &reduced_alphabet_exhaustive},
        std::pair{"limits", &limits},
    };
    for (const auto& [name, run] : tests) {
        if (!run()) {
            std::cerr << "FAILED: " << name << '\n';
            return 1;
        }
    }
    return 0;
}
