#pragma once

#include "compiler/ast_dump.hpp"
#include "compiler/nfa.hpp"

#include <cstddef>
#include <string>

namespace fastregex::compiler {

template <std::size_t StateCapacity, std::size_t EdgeCapacity, std::size_t SetCapacity>
std::string dump_nfa(const nfa<StateCapacity, EdgeCapacity, SetCapacity>& graph) {
    if (graph.entry == no_nfa_state) {
        return "empty\n";
    }
    std::string output;
    for (std::size_t state = 0; state < graph.state_count; ++state) {
        output += "state ";
        detail::append_number(output, state);
        if (state == graph.entry) {
            output += " entry";
        }
        if (graph.states[state].accepting) {
            output += " accept";
        }
        output += '\n';
    }
    for (std::size_t edge = 0; edge < graph.edge_count; ++edge) {
        const auto& item = graph.edges[edge];
        output += "edge ";
        detail::append_number(output, edge);
        output += ' ';
        detail::append_number(output, item.from);
        output += " -> ";
        detail::append_number(output, item.to);
        if (item.kind == nfa_edge_kind::epsilon) {
            output += " epsilon";
        } else {
            output += " bytes ";
            detail::append_set(output, graph.sets[item.set]);
        }
        output += '\n';
    }
    return output;
}

template <std::size_t StateCapacity, std::size_t EdgeCapacity, std::size_t SetCapacity>
std::string dump_nfa_dot(const nfa<StateCapacity, EdgeCapacity, SetCapacity>& graph) {
    std::string output = "digraph nfa {\n  rankdir=LR;\n  start [shape=point];\n";
    for (std::size_t state = 0; state < graph.state_count; ++state) {
        output += "  ";
        detail::append_number(output, state);
        output += graph.states[state].accepting ? " [shape=doublecircle];\n" : " [shape=circle];\n";
    }
    if (graph.entry != no_nfa_state) {
        output += "  start -> ";
        detail::append_number(output, graph.entry);
        output += ";\n";
    }
    for (std::size_t edge = 0; edge < graph.edge_count; ++edge) {
        const auto& item = graph.edges[edge];
        output += "  ";
        detail::append_number(output, item.from);
        output += " -> ";
        detail::append_number(output, item.to);
        output += " [label=\"";
        if (item.kind == nfa_edge_kind::epsilon) {
            output += "epsilon";
        } else {
            detail::append_set(output, graph.sets[item.set]);
        }
        output += "\"];\n";
    }
    output += "}\n";
    return output;
}

} // namespace fastregex::compiler
