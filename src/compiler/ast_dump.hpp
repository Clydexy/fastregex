#pragma once

#include "compiler/ast.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fastregex::compiler {

namespace detail {

inline void append_number(std::string& output, std::size_t number) {
    std::array<char, std::numeric_limits<std::size_t>::digits10 + 1> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
    output.append(buffer.data(), converted.ptr);
}

inline void append_hex(std::string& output, unsigned byte) {
    constexpr std::string_view digits = "0123456789ABCDEF";
    output += digits[byte / 16];
    output += digits[byte % 16];
}

inline void append_set(std::string& output, const byte_set& set) {
    output += '{';
    bool first = true;
    for (unsigned byte = 0; byte < 256; ++byte) {
        if (!set.contains(static_cast<std::uint8_t>(byte))) {
            continue;
        }
        if (!first) {
            output += ',';
        }
        first = false;
        const auto start = byte;
        while (byte < 255 && set.contains(static_cast<std::uint8_t>(byte + 1))) {
            ++byte;
        }
        append_hex(output, start);
        if (byte != start) {
            output += '-';
            append_hex(output, byte);
        }
    }
    output += '}';
}

} // namespace detail

// Arena order and hexadecimal byte ranges make dumps independent of locale,
// pointer addresses, hash iteration order, and source text encoding.
template <std::size_t Capacity> std::string dump_ast(const ast<Capacity>& tree) {
    if (tree.root == no_node) {
        return "empty\n";
    }
    std::string output;
    for (std::size_t id = 0; id < tree.size; ++id) {
        const auto& node = tree.nodes[id];
        detail::append_number(output, id);
        switch (node.kind) {
        case node_kind::epsilon:
            output += " epsilon";
            break;
        case node_kind::bytes:
            output += " bytes";
            break;
        case node_kind::concat:
            output += " concat";
            break;
        case node_kind::alternate:
            output += " alternate";
            break;
        case node_kind::star:
            output += " star";
            break;
        }
        output += " [";
        detail::append_number(output, node.span.begin);
        output += ',';
        detail::append_number(output, node.span.end);
        output += ')';
        if (node.kind == node_kind::bytes) {
            output += ' ';
            detail::append_set(output, tree.sets[node.set]);
        }
        if (node.left != no_node) {
            output += " left=";
            detail::append_number(output, node.left);
        }
        if (node.right != no_node) {
            output += " right=";
            detail::append_number(output, node.right);
        }
        output += '\n';
    }
    output += "root ";
    detail::append_number(output, tree.root);
    output += '\n';
    return output;
}

} // namespace fastregex::compiler
