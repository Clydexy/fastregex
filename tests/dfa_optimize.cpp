#include "compiler/dfa_optimize.hpp"
#include "compiler/dfa_equivalence.hpp"
#include "compiler/dfa_match.hpp"
#include "compiler/parser.hpp"
#include "phase1_fixtures.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>

namespace {

using namespace fastregex::compiler;
using namespace fastregex::test;

constexpr parser_limits parser_test_limits{.ast_nodes = 256};
constexpr nfa_limits nfa_test_limits{.states = 512, .edges = 1'024};
constexpr dfa_limits dfa_test_limits{.states = 128, .transitions = 32'768, .work = 16'777'216};
constexpr equivalence_limits equivalence_test_limits{.pairs = 512, .work = 4'000'000};

constexpr auto compile(std::string_view pattern) {
    const auto parsed = parse<parser_test_limits>(pattern);
    const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
    return determinize<dfa_test_limits>(lowered.graph);
}

template <std::size_t LeftCapacity, std::size_t RightCapacity>
constexpr bool equivalent(const dfa<LeftCapacity>& left, const dfa<RightCapacity>& right) {
    const auto result = equivalent_dfas<equivalence_test_limits>(left, right);
    return result && result.equivalent;
}

constexpr bool class_merging_and_statistics() {
    const auto compiled = compile("a|b");
    const auto merged = merge_byte_classes(compiled.machine);
    const auto optimized = optimize_dfa(compiled.machine);
    return compiled && merged && optimized && equivalent(compiled.machine, merged.machine) &&
           equivalent(compiled.machine, optimized.machine) &&
           merged.machine.byte_class_count == 3 &&
           optimized.statistics.input == dfa_statistics{4, 256, 1'024} &&
           optimized.statistics.after_byte_classes == dfa_statistics{4, 3, 12} &&
           optimized.statistics.after_unreachable_removal == dfa_statistics{4, 3, 12} &&
           optimized.statistics.after_minimization == dfa_statistics{3, 3, 9} &&
           optimized.statistics.output == dfa_statistics{3, 2, 6} &&
           optimized.machine.transition_count == 6;
}

constexpr bool unreachable_removal() {
    const auto compiled = compile("a");
    auto extended = compiled.machine;
    const auto unreachable = static_cast<dfa_state_id>(extended.state_count++);
    extended.states[unreachable].accepting = true;
    extended.states[unreachable].transitions.fill(unreachable);
    extended.transition_count += extended.byte_class_count;
    const auto reduced = remove_unreachable_states(extended);
    return compiled && reduced && reduced.machine.state_count == compiled.machine.state_count &&
           equivalent(extended, reduced.machine);
}

constexpr bool equivalent_state_minimization() {
    const auto compiled = compile("[ab]");
    auto duplicated = compiled.machine;
    const auto original_accept = duplicated.states[duplicated.start].transitions['a'];
    const auto duplicate = static_cast<dfa_state_id>(duplicated.state_count++);
    duplicated.states[duplicate] = duplicated.states[original_accept];
    duplicated.states[duplicated.start].transitions['b'] = duplicate;
    duplicated.transition_count += duplicated.byte_class_count;
    const auto minimized = minimize_dfa(duplicated);
    return compiled && minimized && minimized.machine.state_count == 3 &&
           equivalent(duplicated, minimized.machine);
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
    for (std::size_t byte_class = 0; byte_class < 3; ++byte_class) {
        machine.states[0].transitions[byte_class] = 0;
    }
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

bool generated_pass_preservation() {
    for (std::size_t seed = 0; seed < 324; ++seed) {
        const auto input = reduced_machine(seed);
        const auto classes = merge_byte_classes(input);
        const auto reachable = remove_unreachable_states(input);
        const auto minimized = minimize_dfa(input);
        const auto optimized = optimize_dfa(input);
        const auto repeated = optimize_dfa(input);
        if (!classes || !reachable || !minimized || !optimized || !repeated ||
            !equivalent(input, classes.machine) || !equivalent(input, reachable.machine) ||
            !equivalent(input, minimized.machine) || !equivalent(input, optimized.machine) ||
            optimized.machine != repeated.machine || optimized.statistics != repeated.statistics) {
            std::cerr << "FAILED: generated automaton seed=" << seed << '\n';
            return false;
        }
    }
    return true;
}

template <fastregex::fixed_string Pattern, std::size_t Count>
bool optimized_group(const std::array<match_case, Count>& cases) {
    const auto compiled = compile(Pattern.view());
    const auto optimized = optimize_dfa(compiled.machine);
    if (!compiled || !optimized || !equivalent(compiled.machine, optimized.machine)) {
        std::cerr << "FAILED: optimization pattern=" << Pattern.view() << '\n';
        return false;
    }
    for (const auto& fixture : cases) {
        const auto baseline = full_match(compiled.machine, fixture.text);
        const auto actual = full_match(optimized.machine, fixture.text);
        if (baseline != fixture.expected || actual != fixture.expected) {
            std::cerr << "FAILED: optimized match pattern=" << Pattern.view()
                      << " text_size=" << fixture.text.size() << " expected=" << fixture.expected
                      << " baseline=" << baseline << " actual=" << actual << '\n';
            return false;
        }
    }
    return true;
}

bool regression_corpus() {
    return for_each_match_group([]<fastregex::fixed_string Pattern>(const auto& cases) {
        return optimized_group<Pattern>(cases);
    });
}

constexpr bool constexpr_nullable_cycle() {
    const auto compiled = compile("(|a*)*");
    const auto optimized = optimize_dfa(compiled.machine);
    return compiled && optimized && full_match(optimized.machine, std::string_view{}) &&
           full_match(optimized.machine, std::string_view{"aaaa"}) &&
           !full_match(optimized.machine, std::string_view{"b"});
}

constexpr bool limits() {
    constexpr optimization_limits no_work{.work = 0};
    const auto compiled = compile("a");
    const auto result = optimize_dfa<no_work>(compiled.machine);
    return compiled && !result && result.error.code == error_code::work_exceeded &&
           result.machine.state_count == 0;
}

static_assert(class_merging_and_statistics());
static_assert(unreachable_removal());
static_assert(equivalent_state_minimization());
static_assert(constexpr_nullable_cycle());
static_assert(limits());

} // namespace

int main() {
    const std::array tests{
        std::pair{"classes and statistics", &class_merging_and_statistics},
        std::pair{"unreachable removal", &unreachable_removal},
        std::pair{"state minimization", &equivalent_state_minimization},
        std::pair{"generated pass preservation", &generated_pass_preservation},
        std::pair{"regression corpus", &regression_corpus},
        std::pair{"constexpr nullable cycle", &constexpr_nullable_cycle},
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
