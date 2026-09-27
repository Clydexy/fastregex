#pragma once

#include "compiler/nfa.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fastregex::compiler {

template <std::size_t StateCapacity> struct nfa_state_set {
    std::array<std::uint64_t, (StateCapacity + 63) / 64> words{};

    constexpr bool contains(nfa_state_id state) const {
        return (words[state / 64] & (std::uint64_t{1} << (state % 64))) != 0;
    }

    constexpr bool insert(nfa_state_id state) {
        auto& word = words[state / 64];
        const auto bit = std::uint64_t{1} << (state % 64);
        const bool inserted = (word & bit) == 0;
        word |= bit;
        return inserted;
    }

    constexpr bool operator==(const nfa_state_set&) const = default;
};

struct closure_limits {
    std::size_t work = 16'777'216;
};

template <std::size_t StateCapacity> struct closure_result {
    nfa_state_set<StateCapacity> states{};
    diagnostic error{};
    std::size_t work_used = 0;

    constexpr explicit operator bool() const {
        return error.code == error_code::none;
    }
};

template <closure_limits Limits = closure_limits{}, std::size_t StateCapacity,
          std::size_t EdgeCapacity, std::size_t SetCapacity>
constexpr closure_result<StateCapacity>
    epsilon_closure(const nfa<StateCapacity, EdgeCapacity, SetCapacity>& graph,
                    const nfa_state_set<StateCapacity>& seeds) {
    closure_result<StateCapacity> result;
    std::array<nfa_state_id, StateCapacity> worklist{};
    std::size_t head = 0;
    std::size_t tail = 0;

    const auto charge = [&result](std::size_t amount = 1) constexpr {
        if (!result || amount > Limits.work - result.work_used) {
            if (result) {
                result.error = {error_code::work_exceeded, 0};
            }
            return false;
        }
        result.work_used += amount;
        return true;
    };

    for (std::size_t state = 0; result && state < graph.state_count; ++state) {
        if (!charge()) { // Examine one possible seed.
            break;
        }
        const auto id = static_cast<nfa_state_id>(state);
        if (seeds.contains(id)) {
            result.states.insert(id);
            worklist[tail++] = id;
        }
    }
    while (result && head < tail) {
        if (!charge()) { // Remove one worklist item.
            break;
        }
        const auto state = worklist[head++];
        for (auto edge = graph.states[state].first_edge; result && edge != no_nfa_edge;
             edge = graph.edges[edge].next) {
            if (!charge()) { // Examine one outgoing edge.
                break;
            }
            const auto& candidate = graph.edges[edge];
            if (candidate.kind == nfa_edge_kind::epsilon && result.states.insert(candidate.to)) {
                if (!charge()) { // Insert one worklist item.
                    break;
                }
                worklist[tail++] = candidate.to;
            }
        }
    }
    if (!result) {
        result.states = {};
    }
    return result;
}

template <closure_limits Limits = closure_limits{}, std::size_t StateCapacity,
          std::size_t EdgeCapacity, std::size_t SetCapacity>
constexpr closure_result<StateCapacity>
    epsilon_closure(const nfa<StateCapacity, EdgeCapacity, SetCapacity>& graph, nfa_state_id seed) {
    nfa_state_set<StateCapacity> seeds;
    if (seed < graph.state_count) {
        seeds.insert(seed);
    }
    return epsilon_closure<Limits>(graph, seeds);
}

} // namespace fastregex::compiler
