#include "compiler/epsilon_closure.hpp"
#include "compiler/nfa_dump.hpp"
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

template <std::size_t StateCapacity, std::size_t EdgeCapacity, std::size_t SetCapacity>
constexpr bool accepts(const nfa<StateCapacity, EdgeCapacity, SetCapacity>& graph,
                       std::string_view subject) {
    auto current = epsilon_closure(graph, graph.entry);
    if (!current) {
        return false;
    }
    for (const char character : subject) {
        nfa_state_set<StateCapacity> destinations;
        const auto byte = static_cast<std::uint8_t>(static_cast<unsigned char>(character));
        for (std::size_t state = 0; state < graph.state_count; ++state) {
            const auto id = static_cast<nfa_state_id>(state);
            if (!current.states.contains(id)) {
                continue;
            }
            for (auto edge = graph.states[id].first_edge; edge != no_nfa_edge;
                 edge = graph.edges[edge].next) {
                const auto& item = graph.edges[edge];
                if (item.kind == nfa_edge_kind::bytes && graph.sets[item.set].contains(byte)) {
                    destinations.insert(item.to);
                }
            }
        }
        current = epsilon_closure(graph, destinations);
        if (!current) {
            return false;
        }
    }
    return current.states.contains(graph.accept);
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
    language_case{"ab*", "abb"sv, "ba"sv},
    language_case{"[a-c]", "b"sv, "d"sv},
    language_case{"[^a]", "\0"sv, "a"sv},
    language_case{R"(\x00\xFF)", "\0\xFF"sv, "\xFF"sv},
    language_case{"a|", ""sv, "aa"sv},
    language_case{"|a", "a"sv, "aa"sv},
    language_case{"(|a*)*", "aaaa"sv, "b"sv},
};

constexpr bool languages() {
    for (const auto& fixture : language_cases) {
        const auto parsed = parse<parser_test_limits>(fixture.pattern);
        if (!parsed) {
            return false;
        }
        const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
        if (!lowered || !accepts(lowered.graph, fixture.accepted) ||
            accepts(lowered.graph, fixture.rejected)) {
            return false;
        }
    }
    return true;
}

constexpr bool literal_fragment() {
    const auto parsed = parse<parser_test_limits>("a");
    const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
    if (!lowered || lowered.graph.state_count != 2 || lowered.graph.edge_count != 1 ||
        lowered.graph.set_count != 1 || lowered.graph.entry != 0 || lowered.graph.accept != 1 ||
        lowered.graph.states[0].accepting || !lowered.graph.states[1].accepting) {
        return false;
    }
    const auto& edge = lowered.graph.edges[0];
    return edge.from == 0 && edge.to == 1 && edge.kind == nfa_edge_kind::bytes && edge.set == 0 &&
           lowered.graph.sets[0].contains('a') && !lowered.graph.sets[0].contains('b') &&
           edge.next == no_nfa_edge && lowered.graph.states[0].first_edge == 0 &&
           lowered.graph.states[0].last_edge == 0;
}

constexpr bool epsilon_fragment() {
    const auto parsed = parse<parser_test_limits>("");
    const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
    return lowered && lowered.graph.state_count == 2 && lowered.graph.edge_count == 1 &&
           lowered.graph.set_count == 0 && lowered.graph.entry == 0 && lowered.graph.accept == 1 &&
           lowered.graph.states[1].accepting &&
           lowered.graph.edges[0].kind == nfa_edge_kind::epsilon &&
           lowered.graph.edges[0].from == 0 && lowered.graph.edges[0].to == 1;
}

constexpr bool concat_fragment() {
    const auto parsed = parse<parser_test_limits>("ab");
    const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
    if (!lowered || lowered.graph.state_count != 6 || lowered.graph.edge_count != 5 ||
        lowered.graph.entry != 4 || lowered.graph.accept != 5) {
        return false;
    }
    const auto& edges = lowered.graph.edges;
    return edges[0].from == 0 && edges[0].to == 1 && edges[0].kind == nfa_edge_kind::bytes &&
           edges[1].from == 2 && edges[1].to == 3 && edges[1].kind == nfa_edge_kind::bytes &&
           edges[2].from == 4 && edges[2].to == 0 && edges[3].from == 1 && edges[3].to == 2 &&
           edges[4].from == 3 && edges[4].to == 5;
}

constexpr bool alternate_fragment() {
    const auto parsed = parse<parser_test_limits>("a|b");
    const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
    if (!lowered || lowered.graph.state_count != 6 || lowered.graph.edge_count != 6 ||
        lowered.graph.entry != 4 || lowered.graph.accept != 5) {
        return false;
    }
    const auto& edges = lowered.graph.edges;
    return edges[2].from == 4 && edges[2].to == 0 && edges[3].from == 4 && edges[3].to == 2 &&
           edges[4].from == 1 && edges[4].to == 5 && edges[5].from == 3 && edges[5].to == 5 &&
           lowered.graph.states[4].first_edge == 2 && lowered.graph.edges[2].next == 3 &&
           lowered.graph.edges[3].next == no_nfa_edge;
}

constexpr bool star_fragment() {
    const auto parsed = parse<parser_test_limits>("a*");
    const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
    if (!lowered || lowered.graph.state_count != 4 || lowered.graph.edge_count != 5 ||
        lowered.graph.entry != 2 || lowered.graph.accept != 3) {
        return false;
    }
    const auto& edges = lowered.graph.edges;
    return edges[1].from == 2 && edges[1].to == 3 && edges[2].from == 2 && edges[2].to == 0 &&
           edges[3].from == 1 && edges[3].to == 0 && edges[4].from == 1 && edges[4].to == 3 &&
           lowered.graph.states[2].first_edge == 1 && edges[1].next == 2 &&
           lowered.graph.states[1].first_edge == 3 && edges[3].next == 4;
}

constexpr bool one_accept_state() {
    constexpr std::array patterns{""sv, "a"sv, "ab"sv, "a|b"sv, "(a*)*"sv, "(|a*)*"sv};
    for (const auto pattern : patterns) {
        const auto parsed = parse<parser_test_limits>(pattern);
        const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
        if (!lowered || lowered.graph.accept == no_nfa_state) {
            return false;
        }
        std::size_t accepting = 0;
        for (std::size_t state = 0; state < lowered.graph.state_count; ++state) {
            accepting += lowered.graph.states[state].accepting ? 1U : 0U;
        }
        if (accepting != 1 || !lowered.graph.states[lowered.graph.accept].accepting) {
            return false;
        }
    }
    return true;
}

constexpr bool closures_terminate() {
    constexpr std::array patterns{"a*"sv, "(a*)*"sv, "()*"sv, "(|)*"sv, "(|a*)*"sv};
    for (const auto pattern : patterns) {
        const auto parsed = parse<parser_test_limits>(pattern);
        const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
        if (!lowered) {
            return false;
        }
        const auto initial = epsilon_closure(lowered.graph, lowered.graph.entry);
        if (!initial || !initial.states.contains(lowered.graph.entry) ||
            !initial.states.contains(lowered.graph.accept) ||
            initial.work_used > nfa_test_limits.work) {
            return false;
        }
        for (std::size_t state = 0; state < lowered.graph.state_count; ++state) {
            const auto closure = epsilon_closure(lowered.graph, static_cast<nfa_state_id>(state));
            if (!closure || !closure.states.contains(static_cast<nfa_state_id>(state))) {
                return false;
            }
        }
    }
    return true;
}

template <nfa_limits Limits>
constexpr bool fails_cleanly(std::string_view pattern, error_code code) {
    const auto parsed = parse<parser_test_limits>(pattern);
    const auto lowered = lower_nfa<Limits>(parsed.tree);
    return !lowered && lowered.error == diagnostic{code, 0} &&
           lowered.graph.entry == no_nfa_state && lowered.graph.accept == no_nfa_state &&
           lowered.graph.state_count == 0 && lowered.graph.edge_count == 0 &&
           lowered.graph.set_count == 0;
}

constexpr bool limits() {
    constexpr nfa_limits literal_exact{.states = 2, .edges = 1, .work = 12};
    constexpr nfa_limits one_state{.states = 1, .edges = 1};
    constexpr nfa_limits no_edges{.states = 2, .edges = 0};
    constexpr nfa_limits short_work{.states = 2, .edges = 1, .work = 11};
    constexpr nfa_limits concat_exact{.states = 6, .edges = 5};
    constexpr nfa_limits concat_short_states{.states = 5, .edges = 5};
    constexpr nfa_limits concat_short_edges{.states = 6, .edges = 4};
    const auto parsed = parse<parser_test_limits>("a");
    const auto admitted = lower_nfa<literal_exact>(parsed.tree);
    return admitted && admitted.work_used == 12 &&
           fails_cleanly<one_state>("a", error_code::nfa_states_exceeded) &&
           fails_cleanly<no_edges>("a", error_code::nfa_edges_exceeded) &&
           fails_cleanly<short_work>("a", error_code::work_exceeded) &&
           lower_nfa<concat_exact>(parse<parser_test_limits>("ab").tree) &&
           fails_cleanly<concat_short_states>("ab", error_code::nfa_states_exceeded) &&
           fails_cleanly<concat_short_edges>("ab", error_code::nfa_edges_exceeded);
}

constexpr bool closure_limit() {
    constexpr closure_limits exact{.work = 12};
    constexpr closure_limits short_limit{.work = 11};
    const auto parsed = parse<parser_test_limits>("a*");
    const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
    const auto admitted = epsilon_closure<exact>(lowered.graph, lowered.graph.entry);
    const auto rejected = epsilon_closure<short_limit>(lowered.graph, lowered.graph.entry);
    return admitted && admitted.work_used == 12 && admitted.states.contains(lowered.graph.accept) &&
           !rejected && rejected.error == diagnostic{error_code::work_exceeded, 0} &&
           rejected.states == nfa_state_set<nfa_test_limits.states>{};
}

constexpr auto representative_ast = parse<parser_test_limits>("a(b|[c-e])*");
constexpr auto representative_nfa = lower_nfa<nfa_test_limits>(representative_ast.tree);

static_assert(languages());
static_assert(literal_fragment());
static_assert(epsilon_fragment());
static_assert(concat_fragment());
static_assert(alternate_fragment());
static_assert(star_fragment());
static_assert(one_accept_state());
static_assert(closures_terminate());
static_assert(limits());
static_assert(closure_limit());
static_assert(representative_ast && representative_nfa);
static_assert(representative_nfa.graph.state_count == 12);
static_assert(representative_nfa.graph.edge_count == 14);
static_assert(accepts(representative_nfa.graph, "abdecc"));
static_assert(!accepts(representative_nfa.graph, "acx"));

bool dumps() {
    const auto parsed = parse<parser_test_limits>("a*");
    const auto lowered = lower_nfa<nfa_test_limits>(parsed.tree);
    constexpr std::string_view text = "state 0\n"
                                      "state 1\n"
                                      "state 2 entry\n"
                                      "state 3 accept\n"
                                      "edge 0 0 -> 1 bytes {61}\n"
                                      "edge 1 2 -> 3 epsilon\n"
                                      "edge 2 2 -> 0 epsilon\n"
                                      "edge 3 1 -> 0 epsilon\n"
                                      "edge 4 1 -> 3 epsilon\n";
    constexpr std::string_view dot = "digraph nfa {\n"
                                     "  rankdir=LR;\n"
                                     "  start [shape=point];\n"
                                     "  0 [shape=circle];\n"
                                     "  1 [shape=circle];\n"
                                     "  2 [shape=circle];\n"
                                     "  3 [shape=doublecircle];\n"
                                     "  start -> 2;\n"
                                     "  0 -> 1 [label=\"{61}\"];\n"
                                     "  2 -> 3 [label=\"epsilon\"];\n"
                                     "  2 -> 0 [label=\"epsilon\"];\n"
                                     "  1 -> 0 [label=\"epsilon\"];\n"
                                     "  1 -> 3 [label=\"epsilon\"];\n"
                                     "}\n";
    return lowered && dump_nfa(lowered.graph) == text && dump_nfa(lowered.graph) == text &&
           dump_nfa_dot(lowered.graph) == dot &&
           dump_nfa(lower_nfa<nfa_test_limits>(parse<parser_test_limits>("a*").tree).graph) == text;
}

bool runtime_cases() {
    return languages() && literal_fragment() && epsilon_fragment() && concat_fragment() &&
           alternate_fragment() && star_fragment() && one_accept_state() && closures_terminate() &&
           limits() && closure_limit() && accepts(representative_nfa.graph, "a") &&
           accepts(representative_nfa.graph, "abccde") && !accepts(representative_nfa.graph, "b");
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
