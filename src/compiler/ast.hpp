#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fastregex::compiler {

using node_id = std::uint16_t;
inline constexpr node_id no_node = std::numeric_limits<node_id>::max();

struct source_span {
    std::uint16_t begin = 0;
    std::uint16_t end = 0;
    constexpr bool operator==(const source_span&) const = default;
};

struct byte_set {
    std::array<std::uint64_t, 4> words{};

    constexpr void insert(std::uint8_t byte) {
        words[byte / 64] |= std::uint64_t{1} << (byte % 64);
    }

    constexpr bool contains(std::uint8_t byte) const {
        return (words[byte / 64] & (std::uint64_t{1} << (byte % 64))) != 0;
    }

    constexpr void complement() {
        for (auto& word : words) {
            word = ~word;
        }
    }

    constexpr bool operator==(const byte_set&) const = default;
};

enum class node_kind : std::uint8_t { epsilon, bytes, concat, alternate, star };

struct ast_node {
    node_kind kind = node_kind::epsilon;
    source_span span{};
    node_id left = no_node;
    node_id right = no_node;
    node_id set = no_node;
};

// Only [0, size) and [0, set_count) are populated. Child IDs precede parents.
// Groups widen their root's span instead of adding a node.
template <std::size_t Capacity> struct ast {
    static_assert(Capacity < no_node);
    std::array<ast_node, Capacity> nodes{};
    std::array<byte_set, Capacity> sets{};
    std::size_t size = 0;
    std::size_t set_count = 0;
    node_id root = no_node;
};

} // namespace fastregex::compiler
