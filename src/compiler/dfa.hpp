#pragma once

#include "compiler/epsilon_closure.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace fastregex::compiler {

using dfa_state_id = std::uint16_t;

inline constexpr dfa_state_id no_dfa_state = std::numeric_limits<dfa_state_id>::max();
inline constexpr std::size_t byte_alphabet_size = 256;

struct dfa_state {
    std::array<dfa_state_id, byte_alphabet_size> transitions{};
    bool accepting = false;

    constexpr bool operator==(const dfa_state&) const = default;
};

template <std::size_t StateCapacity> struct dfa {
    static_assert(StateCapacity < no_dfa_state);

    std::array<dfa_state, StateCapacity> states{};
    std::size_t state_count = 0;
    std::size_t transition_count = 0;
    dfa_state_id start = no_dfa_state;
    dfa_state_id sink = no_dfa_state;

    constexpr bool operator==(const dfa&) const = default;
};

struct dfa_limits {
    std::size_t states = 4'096;
    std::size_t transitions = 1'048'576;
    std::size_t work = 16'777'216;
};

template <std::size_t StateCapacity> struct determinize_result {
    dfa<StateCapacity> machine{};
    diagnostic error{};
    std::size_t work_used = 0;

    constexpr explicit operator bool() const {
        return error.code == error_code::none;
    }
};

namespace detail {

template <dfa_limits Limits, std::size_t NfaStateCapacity, std::size_t NfaEdgeCapacity,
          std::size_t NfaSetCapacity>
class dfa_builder {
    static_assert(Limits.states < no_dfa_state);

    using state_set = nfa_state_set<NfaStateCapacity>;

    const nfa<NfaStateCapacity, NfaEdgeCapacity, NfaSetCapacity>& graph_;
    determinize_result<Limits.states>& result_;
    std::vector<state_set> subsets_;

    constexpr std::size_t active_word_count() const {
        return (graph_.state_count + 63) / 64;
    }

    constexpr void fail(error_code code) {
        if (result_) {
            result_.error = {code, 0};
        }
    }

    constexpr bool charge(std::size_t amount = 1) {
        if (!result_) {
            return false;
        }
        if (amount > Limits.work - result_.work_used) {
            fail(error_code::work_exceeded);
            return false;
        }
        result_.work_used += amount;
        return true;
    }

    constexpr state_set close(const state_set& seeds) {
        state_set closure;
        std::array<nfa_state_id, NfaStateCapacity> worklist{};
        std::size_t head = 0;
        std::size_t tail = 0;

        for (std::size_t state = 0; result_ && state < graph_.state_count; ++state) {
            if (!charge()) {
                break;
            }
            const auto id = static_cast<nfa_state_id>(state);
            if (seeds.contains(id)) {
                closure.insert(id);
                worklist[tail++] = id;
            }
        }
        while (result_ && head < tail) {
            if (!charge()) {
                break;
            }
            const auto state = worklist[head++];
            for (auto edge = graph_.states[state].first_edge; result_ && edge != no_nfa_edge;
                 edge = graph_.edges[edge].next) {
                if (!charge()) {
                    break;
                }
                const auto& candidate = graph_.edges[edge];
                if (candidate.kind == nfa_edge_kind::epsilon && closure.insert(candidate.to)) {
                    if (!charge()) {
                        break;
                    }
                    worklist[tail++] = candidate.to;
                }
            }
        }
        return closure;
    }

    constexpr state_set transition(const state_set& source, std::uint8_t byte) {
        state_set seeds;
        for (std::size_t state = 0; result_ && state < graph_.state_count; ++state) {
            if (!charge()) {
                break;
            }
            const auto id = static_cast<nfa_state_id>(state);
            if (!source.contains(id)) {
                continue;
            }
            for (auto edge = graph_.states[id].first_edge; result_ && edge != no_nfa_edge;
                 edge = graph_.edges[edge].next) {
                if (!charge()) {
                    break;
                }
                const auto& candidate = graph_.edges[edge];
                if (candidate.kind == nfa_edge_kind::bytes &&
                    graph_.sets[candidate.set].contains(byte)) {
                    seeds.insert(candidate.to);
                }
            }
        }
        return result_ ? close(seeds) : state_set{};
    }

    constexpr bool is_accepting(const state_set& subset) {
        for (std::size_t state = 0; result_ && state < graph_.state_count; ++state) {
            if (!charge()) {
                return false;
            }
            const auto id = static_cast<nfa_state_id>(state);
            if (subset.contains(id) && graph_.states[id].accepting) {
                return true;
            }
        }
        return false;
    }

    constexpr bool same_subset(const state_set& left, const state_set& right) {
        for (std::size_t word = 0; result_ && word < active_word_count(); ++word) {
            if (!charge()) {
                return false;
            }
            if (left.words[word] != right.words[word]) {
                return false;
            }
        }
        return static_cast<bool>(result_);
    }

    constexpr bool subset_empty(const state_set& subset) {
        for (std::size_t word = 0; result_ && word < active_word_count(); ++word) {
            if (!charge()) {
                return false;
            }
            if (subset.words[word] != 0) {
                return false;
            }
        }
        return true;
    }

    constexpr dfa_state_id add_state(const state_set& subset, bool accepting) {
        if (!charge(1 + byte_alphabet_size + active_word_count())) {
            return no_dfa_state;
        }
        auto& machine = result_.machine;
        if (machine.state_count == Limits.states) {
            fail(error_code::dfa_states_exceeded);
            return no_dfa_state;
        }
        if (byte_alphabet_size > Limits.transitions - machine.transition_count) {
            fail(error_code::transitions_exceeded);
            return no_dfa_state;
        }
        const auto id = static_cast<dfa_state_id>(machine.state_count++);
        machine.transition_count += byte_alphabet_size;
        machine.states[id].accepting = accepting;
        machine.states[id].transitions.fill(machine.sink);
        subsets_.push_back(subset);
        return id;
    }

    constexpr dfa_state_id intern(const state_set& subset) {
        auto& machine = result_.machine;
        for (std::size_t state = 1; result_ && state < machine.state_count; ++state) {
            if (same_subset(subsets_[state], subset)) {
                return static_cast<dfa_state_id>(state);
            }
        }
        if (!result_) {
            return no_dfa_state;
        }
        const bool accepting = is_accepting(subset);
        return result_ ? add_state(subset, accepting) : no_dfa_state;
    }

  public:
    constexpr dfa_builder(const nfa<NfaStateCapacity, NfaEdgeCapacity, NfaSetCapacity>& graph,
                          determinize_result<Limits.states>& result)
        : graph_(graph), result_(result) {}

    constexpr void run() {
        if (graph_.entry >= graph_.state_count || graph_.accept >= graph_.state_count) {
            fail(error_code::invalid_pattern);
            return;
        }

        state_set empty;
        result_.machine.sink = 0;
        const auto sink = add_state(empty, false);
        if (result_ && sink != 0) {
            fail(error_code::invalid_pattern);
        }

        state_set entry;
        entry.insert(graph_.entry);
        const auto initial = close(entry);
        if (result_) {
            const bool empty_initial = subset_empty(initial);
            if (result_) {
                result_.machine.start = empty_initial ? sink : intern(initial);
            }
        }

        for (std::size_t source = 1; result_ && source < result_.machine.state_count; ++source) {
            for (std::size_t byte = 0; result_ && byte < byte_alphabet_size; ++byte) {
                const auto destination =
                    transition(subsets_[source], static_cast<std::uint8_t>(byte));
                if (!result_) {
                    break;
                }
                const bool empty_destination = subset_empty(destination);
                const auto target = empty_destination ? sink : intern(destination);
                if (result_) {
                    result_.machine.states[source].transitions[byte] = target;
                }
            }
        }

        if (!result_) {
            result_.machine = {};
        }
    }
};

} // namespace detail

template <dfa_limits Limits = dfa_limits{}, std::size_t NfaStateCapacity,
          std::size_t NfaEdgeCapacity, std::size_t NfaSetCapacity>
constexpr determinize_result<Limits.states>
    determinize(const nfa<NfaStateCapacity, NfaEdgeCapacity, NfaSetCapacity>& graph) {
    determinize_result<Limits.states> result;
    detail::dfa_builder<Limits, NfaStateCapacity, NfaEdgeCapacity, NfaSetCapacity>{graph, result}
        .run();
    return result;
}

} // namespace fastregex::compiler
