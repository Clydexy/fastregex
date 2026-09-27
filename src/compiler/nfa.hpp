#pragma once

#include "compiler/ast.hpp"
#include "compiler/diagnostic.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fastregex::compiler {

using nfa_state_id = std::uint16_t;
using nfa_edge_id = std::uint16_t;
using nfa_set_id = std::uint16_t;

inline constexpr nfa_state_id no_nfa_state = std::numeric_limits<nfa_state_id>::max();
inline constexpr nfa_edge_id no_nfa_edge = std::numeric_limits<nfa_edge_id>::max();
inline constexpr nfa_set_id no_nfa_set = std::numeric_limits<nfa_set_id>::max();

enum class nfa_edge_kind : std::uint8_t { epsilon, bytes };

struct nfa_state {
    nfa_edge_id first_edge = no_nfa_edge;
    nfa_edge_id last_edge = no_nfa_edge;
    bool accepting = false;
};

struct nfa_edge {
    nfa_state_id from = no_nfa_state;
    nfa_state_id to = no_nfa_state;
    nfa_edge_id next = no_nfa_edge;
    nfa_set_id set = no_nfa_set;
    nfa_edge_kind kind = nfa_edge_kind::epsilon;
};

template <std::size_t StateCapacity, std::size_t EdgeCapacity, std::size_t SetCapacity> struct nfa {
    static_assert(StateCapacity < no_nfa_state);
    static_assert(EdgeCapacity < no_nfa_edge);
    static_assert(SetCapacity < no_nfa_set);

    std::array<nfa_state, StateCapacity> states{};
    std::array<nfa_edge, EdgeCapacity> edges{};
    std::array<byte_set, SetCapacity> sets{};
    std::size_t state_count = 0;
    std::size_t edge_count = 0;
    std::size_t set_count = 0;
    nfa_state_id entry = no_nfa_state;
    nfa_state_id accept = no_nfa_state;
};

struct nfa_limits {
    std::size_t states = 16'384;
    std::size_t edges = 32'768;
    std::size_t work = 16'777'216;
};

template <std::size_t StateCapacity, std::size_t EdgeCapacity, std::size_t SetCapacity>
struct lower_result {
    nfa<StateCapacity, EdgeCapacity, SetCapacity> graph{};
    diagnostic error{};
    std::size_t work_used = 0;

    constexpr explicit operator bool() const {
        return error.code == error_code::none;
    }
};

namespace detail {

struct nfa_fragment {
    nfa_state_id entry = no_nfa_state;
    nfa_state_id exit = no_nfa_state;
};

template <nfa_limits Limits, std::size_t AstCapacity> class nfa_builder {
    static_assert(Limits.states <= 16'384);
    static_assert(Limits.edges <= 32'768);

    const ast<AstCapacity>& tree_;
    lower_result<Limits.states, Limits.edges, AstCapacity>& result_;
    std::array<nfa_fragment, AstCapacity> fragments_{};

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

    constexpr nfa_state_id add_state() {
        if (!charge()) {
            return no_nfa_state;
        }
        auto& graph = result_.graph;
        if (graph.state_count == Limits.states) {
            fail(error_code::nfa_states_exceeded);
            return no_nfa_state;
        }
        return static_cast<nfa_state_id>(graph.state_count++);
    }

    constexpr bool add_edge(nfa_state_id from, nfa_state_id to, nfa_edge_kind kind,
                            nfa_set_id set = no_nfa_set) {
        if (!charge()) {
            return false;
        }
        auto& graph = result_.graph;
        if (graph.edge_count == Limits.edges) {
            fail(error_code::nfa_edges_exceeded);
            return false;
        }
        const auto id = static_cast<nfa_edge_id>(graph.edge_count++);
        graph.edges[id] = {from, to, no_nfa_edge, set, kind};
        auto& state = graph.states[from];
        if (state.first_edge == no_nfa_edge) {
            state.first_edge = id;
        } else {
            graph.edges[state.last_edge].next = id;
        }
        state.last_edge = id;
        return true;
    }

    constexpr nfa_set_id copy_set(const byte_set& set) {
        // Four reads and four writes.
        if (!charge(8)) {
            return no_nfa_set;
        }
        auto& graph = result_.graph;
        const auto id = static_cast<nfa_set_id>(graph.set_count++);
        graph.sets[id] = set;
        return id;
    }

    constexpr nfa_fragment begin_fragment() {
        const auto entry = add_state();
        const auto exit = add_state();
        return {entry, exit};
    }

    constexpr void lower_node(node_id id) {
        if (!charge()) { // Visit the AST node.
            return;
        }
        const auto& node = tree_.nodes[id];
        const auto fragment = begin_fragment();
        if (!result_) {
            return;
        }
        fragments_[id] = fragment;
        switch (node.kind) {
        case node_kind::epsilon:
            add_edge(fragment.entry, fragment.exit, nfa_edge_kind::epsilon);
            break;
        case node_kind::bytes: {
            const auto set = copy_set(tree_.sets[node.set]);
            if (result_) {
                add_edge(fragment.entry, fragment.exit, nfa_edge_kind::bytes, set);
            }
            break;
        }
        case node_kind::concat: {
            const auto left = fragments_[node.left];
            const auto right = fragments_[node.right];
            add_edge(fragment.entry, left.entry, nfa_edge_kind::epsilon);
            add_edge(left.exit, right.entry, nfa_edge_kind::epsilon);
            add_edge(right.exit, fragment.exit, nfa_edge_kind::epsilon);
            break;
        }
        case node_kind::alternate: {
            const auto left = fragments_[node.left];
            const auto right = fragments_[node.right];
            add_edge(fragment.entry, left.entry, nfa_edge_kind::epsilon);
            add_edge(fragment.entry, right.entry, nfa_edge_kind::epsilon);
            add_edge(left.exit, fragment.exit, nfa_edge_kind::epsilon);
            add_edge(right.exit, fragment.exit, nfa_edge_kind::epsilon);
            break;
        }
        case node_kind::star: {
            const auto child = fragments_[node.left];
            add_edge(fragment.entry, fragment.exit, nfa_edge_kind::epsilon);
            add_edge(fragment.entry, child.entry, nfa_edge_kind::epsilon);
            add_edge(child.exit, child.entry, nfa_edge_kind::epsilon);
            add_edge(child.exit, fragment.exit, nfa_edge_kind::epsilon);
            break;
        }
        }
    }

  public:
    constexpr nfa_builder(const ast<AstCapacity>& tree,
                          lower_result<Limits.states, Limits.edges, AstCapacity>& result)
        : tree_(tree), result_(result) {}

    constexpr void run() {
        if (tree_.root == no_node || tree_.root >= tree_.size) {
            fail(error_code::invalid_pattern);
            return;
        }
        for (std::size_t id = 0; result_ && id < tree_.size; ++id) {
            lower_node(static_cast<node_id>(id));
        }
        if (result_) {
            const auto root = fragments_[tree_.root];
            result_.graph.entry = root.entry;
            result_.graph.accept = root.exit;
            result_.graph.states[root.exit].accepting = true;
        } else {
            // Capacity failures never expose a truncated graph.
            result_.graph = {};
        }
    }
};

} // namespace detail

template <nfa_limits Limits = nfa_limits{}, std::size_t AstCapacity>
constexpr lower_result<Limits.states, Limits.edges, AstCapacity>
    lower_nfa(const ast<AstCapacity>& tree) {
    lower_result<Limits.states, Limits.edges, AstCapacity> result;
    detail::nfa_builder<Limits, AstCapacity>{tree, result}.run();
    return result;
}

} // namespace fastregex::compiler
