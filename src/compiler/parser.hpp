#pragma once

#include "compiler/ast.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fastregex::compiler {

enum class error_code : std::uint8_t {
    none,
    invalid_pattern,
    pattern_bytes_exceeded,
    nesting_exceeded,
    ast_nodes_exceeded,
    work_exceeded
};

struct diagnostic {
    error_code code = error_code::none;
    std::size_t offset = 0;
    constexpr bool operator==(const diagnostic&) const = default;
};

// Internal compile-time overrides keep boundary tests small; no public options API.
struct parser_limits {
    std::size_t pattern_bytes = 4096;
    std::size_t nesting = 64;
    std::size_t ast_nodes = 8192;
    std::size_t work = 16'777'216;
};

template <std::size_t Capacity> struct parse_result {
    ast<Capacity> tree{};
    diagnostic error{};
    std::size_t work_used = 0;

    constexpr explicit operator bool() const {
        return error.code == error_code::none;
    }
};

namespace detail {

template <parser_limits Limits> class parser {
    static_assert(Limits.pattern_bytes <= 4096);
    static_assert(Limits.nesting <= 64);
    static_assert(Limits.ast_nodes <= 8192);

    struct frame {
        node_id alternative = no_node;
        node_id sequence = no_node;
        std::size_t begin = 0;
    };

    std::string_view pattern_;
    parse_result<Limits.ast_nodes>& result_;
    std::array<frame, Limits.nesting + 1> frames_{};
    std::size_t depth_ = 0;
    std::size_t cursor_ = 0;

    constexpr void fail(error_code code, std::size_t offset = 0) {
        if (result_) {
            result_.error = {code, offset};
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

    constexpr bool at_end() const {
        return cursor_ == pattern_.size();
    }

    constexpr std::uint8_t peek() const {
        return static_cast<std::uint8_t>(static_cast<unsigned char>(pattern_[cursor_]));
    }

    constexpr bool consume() {
        if (!charge()) {
            return false;
        }
        ++cursor_;
        return true;
    }

    static constexpr source_span span(std::size_t begin, std::size_t end) {
        // Pattern length is checked before parsing, so both offsets fit.
        return {static_cast<std::uint16_t>(begin), static_cast<std::uint16_t>(end)};
    }

    constexpr node_id add_node(node_kind kind, source_span location, node_id left = no_node,
                               node_id right = no_node) {
        if (!charge()) {
            return no_node;
        }
        auto& tree = result_.tree;
        if (tree.size == Limits.ast_nodes) {
            fail(error_code::ast_nodes_exceeded);
            return no_node;
        }
        const auto id = static_cast<node_id>(tree.size++);
        tree.nodes[id] = {kind, location, left, right, no_node};
        return id;
    }

    constexpr node_id join(node_kind kind, node_id left, node_id right) {
        if (!result_ || left == no_node || right == no_node) {
            return no_node;
        }
        if (!charge(2)) { // Visit both children.
            return no_node;
        }
        return add_node(kind,
                        {result_.tree.nodes[left].span.begin, result_.tree.nodes[right].span.end},
                        left, right);
    }

    static constexpr int hex_digit(std::uint8_t byte) {
        if (byte >= '0' && byte <= '9') {
            return byte - '0';
        }
        if (byte >= 'a' && byte <= 'f') {
            return byte - 'a' + 10;
        }
        if (byte >= 'A' && byte <= 'F') {
            return byte - 'A' + 10;
        }
        return -1;
    }

    // Read a literal or hex byte. Class syntax is handled separately.
    constexpr bool read_byte(std::uint8_t& byte, bool in_class) {
        if (at_end()) {
            fail(error_code::invalid_pattern, cursor_);
            return false;
        }
        byte = peek();
        if (byte == '\\') {
            if (!consume()) {
                return false;
            }
            if (at_end() || peek() != 'x') {
                fail(error_code::invalid_pattern, cursor_);
                return false;
            }
            if (!consume()) {
                return false;
            }
            unsigned value = 0;
            for (unsigned digit = 0; digit < 2; ++digit) {
                if (at_end() || hex_digit(peek()) < 0) {
                    fail(error_code::invalid_pattern, cursor_);
                    return false;
                }
                value = value * 16 + static_cast<unsigned>(hex_digit(peek()));
                if (!consume()) {
                    return false;
                }
            }
            byte = static_cast<std::uint8_t>(value);
            return true;
        }
        const bool reserved = in_class ? byte == '[' || byte == ']' || byte == '^' || byte == '-'
                                       : std::string_view{"()|*[].^$+?{}"}.find(
                                             static_cast<char>(byte)) != std::string_view::npos;
        if (reserved) {
            fail(error_code::invalid_pattern, cursor_);
            return false;
        }
        return consume();
    }

    constexpr bool insert(byte_set& set, std::uint8_t byte) {
        if (!charge(2)) { // One word read and one word write.
            return false;
        }
        set.insert(byte);
        return true;
    }

    constexpr bool read_class(byte_set& set) {
        if (!consume()) { // Opening '['.
            return false;
        }
        const bool complemented = !at_end() && peek() == '^';
        if (complemented && !consume()) {
            return false;
        }
        bool has_item = false;
        while (result_ && !at_end() && peek() != ']') {
            std::uint8_t first = 0;
            if (!read_byte(first, true)) {
                return false;
            }
            std::uint8_t last = first;
            if (!at_end() && peek() == '-') {
                if (!consume()) {
                    return false;
                }
                const auto endpoint = cursor_;
                if (!read_byte(last, true)) {
                    return false;
                }
                if (last < first) {
                    fail(error_code::invalid_pattern, endpoint);
                    return false;
                }
            }
            // A wider counter prevents wraparound at byte FF.
            for (unsigned value = first; value <= last; ++value) {
                if (!insert(set, static_cast<std::uint8_t>(value))) {
                    return false;
                }
            }
            has_item = true;
        }
        if (!has_item || at_end()) {
            fail(error_code::invalid_pattern, cursor_);
            return false;
        }
        if (!consume()) {
            return false;
        }
        if (complemented) {
            if (!charge(8)) {
                return false;
            }
            set.complement();
        }
        return true;
    }

    constexpr node_id read_set() {
        const auto begin = cursor_;
        if (!charge(4)) { // Initialize four set words.
            return no_node;
        }
        byte_set set{};
        if (peek() == '[') {
            if (!read_class(set)) {
                return no_node;
            }
        } else {
            std::uint8_t byte = 0;
            if (!read_byte(byte, false) || !insert(set, byte)) {
                return no_node;
            }
        }
        if (!charge(8)) { // Copy four set words into the arena.
            return no_node;
        }
        const auto id = add_node(node_kind::bytes, span(begin, cursor_));
        if (id != no_node) {
            auto& tree = result_.tree;
            // Every set owns a node, so the node capacity also bounds this arena.
            tree.nodes[id].set = static_cast<node_id>(tree.set_count);
            tree.sets[tree.set_count++] = set;
        }
        return id;
    }

    constexpr void append_atom(node_id atom) {
        if (!result_) {
            return;
        }
        if (!at_end() && peek() == '*') {
            if (!consume() || !charge()) { // Visit the operand.
                return;
            }
            atom =
                add_node(node_kind::star, span(result_.tree.nodes[atom].span.begin, cursor_), atom);
        }
        auto& sequence = frames_[depth_].sequence;
        sequence = sequence == no_node ? atom : join(node_kind::concat, sequence, atom);
    }

    constexpr node_id finish_alternative() {
        auto& current = frames_[depth_];
        auto sequence = current.sequence;
        if (sequence == no_node) {
            sequence = add_node(node_kind::epsilon, span(cursor_, cursor_));
        }
        const auto combined = current.alternative == no_node
                                  ? sequence
                                  : join(node_kind::alternate, current.alternative, sequence);
        current.alternative = combined;
        current.sequence = no_node;
        return combined;
    }

  public:
    constexpr parser(std::string_view pattern, parse_result<Limits.ast_nodes>& result)
        : pattern_(pattern), result_(result) {}

    constexpr void run() {
        if (pattern_.size() > Limits.pattern_bytes) {
            fail(error_code::pattern_bytes_exceeded);
            return;
        }
        while (result_ && !at_end()) {
            switch (peek()) {
            case '(':
                if (!consume() || !charge()) { // Push a group frame.
                    break;
                }
                if (depth_ == Limits.nesting) {
                    fail(error_code::nesting_exceeded);
                    break;
                }
                frames_[++depth_] = {no_node, no_node, cursor_ - 1};
                break;
            case ')': {
                if (depth_ == 0) {
                    fail(error_code::invalid_pattern, cursor_);
                    break;
                }
                const auto root = finish_alternative();
                if (!result_ || !consume() || !charge()) { // Pop a group frame.
                    break;
                }
                result_.tree.nodes[root].span = span(frames_[depth_].begin, cursor_);
                --depth_;
                append_atom(root);
                break;
            }
            case '|':
                finish_alternative();
                if (result_) {
                    consume();
                }
                break;
            default:
                append_atom(read_set());
                break;
            }
        }
        if (result_ && depth_ != 0) {
            fail(error_code::invalid_pattern, cursor_);
        }
        if (result_) {
            result_.tree.root = finish_alternative();
        }
        if (!result_) {
            // No partial tree is exposed as a successful result.
            result_.tree.root = no_node;
            result_.tree.size = 0;
            result_.tree.set_count = 0;
        }
    }
};

} // namespace detail

template <parser_limits Limits = parser_limits{}>
constexpr parse_result<Limits.ast_nodes> parse(std::string_view pattern) {
    parse_result<Limits.ast_nodes> result;
    detail::parser<Limits>{pattern, result}.run();
    return result;
}

// Literal adapter: retain embedded NUL and exclude only the final terminator.
template <parser_limits Limits = parser_limits{}, std::size_t Size>
constexpr parse_result<Limits.ast_nodes> parse(const char (&pattern)[Size]) {
    static_assert(Size > 0);
    return parse<Limits>(std::string_view{pattern, Size - 1});
}

} // namespace fastregex::compiler
