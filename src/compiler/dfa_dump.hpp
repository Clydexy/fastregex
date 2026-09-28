#pragma once

#include "compiler/ast_dump.hpp"
#include "compiler/dfa.hpp"

#include <cstddef>
#include <string>

namespace fastregex::compiler {

namespace detail {

inline void append_byte_range(std::string& output, std::size_t first, std::size_t last) {
    append_hex(output, static_cast<unsigned>(first));
    if (first != last) {
        output += '-';
        append_hex(output, static_cast<unsigned>(last));
    }
}

} // namespace detail

template <std::size_t StateCapacity> std::string dump_dfa(const dfa<StateCapacity>& machine) {
    if (machine.start == no_dfa_state) {
        return "empty\n";
    }
    std::string output;
    for (std::size_t state = 0; state < machine.state_count; ++state) {
        output += "state ";
        detail::append_number(output, state);
        if (state == machine.start) {
            output += " start";
        }
        if (state == machine.sink) {
            output += " sink";
        }
        if (machine.states[state].accepting) {
            output += " accept";
        }
        output += '\n';
    }
    for (std::size_t state = 0; state < machine.state_count; ++state) {
        for (std::size_t first = 0; first < byte_alphabet_size;) {
            auto last = first;
            const auto target = transition_for_byte(machine, static_cast<dfa_state_id>(state),
                                                    static_cast<std::uint8_t>(first));
            while (last + 1 < byte_alphabet_size &&
                   transition_for_byte(machine, static_cast<dfa_state_id>(state),
                                       static_cast<std::uint8_t>(last + 1)) == target) {
                ++last;
            }
            output += "transition ";
            detail::append_number(output, state);
            output += ' ';
            detail::append_byte_range(output, first, last);
            output += " -> ";
            detail::append_number(output, target);
            output += '\n';
            first = last + 1;
        }
    }
    return output;
}

template <std::size_t StateCapacity> std::string dump_dfa_dot(const dfa<StateCapacity>& machine) {
    std::string output = "digraph dfa {\n  rankdir=LR;\n  start [shape=point];\n";
    for (std::size_t state = 0; state < machine.state_count; ++state) {
        output += "  ";
        detail::append_number(output, state);
        output +=
            machine.states[state].accepting ? " [shape=doublecircle];\n" : " [shape=circle];\n";
    }
    if (machine.start != no_dfa_state) {
        output += "  start -> ";
        detail::append_number(output, machine.start);
        output += ";\n";
    }
    for (std::size_t state = 0; state < machine.state_count; ++state) {
        for (std::size_t first = 0; first < byte_alphabet_size;) {
            auto last = first;
            const auto target = transition_for_byte(machine, static_cast<dfa_state_id>(state),
                                                    static_cast<std::uint8_t>(first));
            while (last + 1 < byte_alphabet_size &&
                   transition_for_byte(machine, static_cast<dfa_state_id>(state),
                                       static_cast<std::uint8_t>(last + 1)) == target) {
                ++last;
            }
            output += "  ";
            detail::append_number(output, state);
            output += " -> ";
            detail::append_number(output, target);
            output += " [label=\"";
            detail::append_byte_range(output, first, last);
            output += "\"];\n";
            first = last + 1;
        }
    }
    output += "}\n";
    return output;
}

} // namespace fastregex::compiler
