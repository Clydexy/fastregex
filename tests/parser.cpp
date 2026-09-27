#include "compiler/parser.hpp"
#include "compiler/ast_dump.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace fastregex::compiler;
using namespace std::string_view_literals;
constexpr parser_limits small{.ast_nodes = 64};

struct valid_case {
    std::string_view pattern;
    std::size_t nodes;
    node_kind root;
};

constexpr std::array valid_cases{
    valid_case{"", 1, node_kind::epsilon},         valid_case{"a", 1, node_kind::bytes},
    valid_case{"ab", 3, node_kind::concat},        valid_case{"abc", 5, node_kind::concat},
    valid_case{"a|bc", 5, node_kind::alternate},   valid_case{"a|b|c", 5, node_kind::alternate},
    valid_case{"ab|cd*", 8, node_kind::alternate}, valid_case{"(ab)*", 4, node_kind::star},
    valid_case{"ab*", 4, node_kind::concat},       valid_case{"()", 1, node_kind::epsilon},
    valid_case{"(())", 1, node_kind::epsilon},     valid_case{"()*", 2, node_kind::star},
    valid_case{"(a*)*", 3, node_kind::star},       valid_case{"a|", 3, node_kind::alternate},
    valid_case{"|a", 3, node_kind::alternate},     valid_case{"|", 3, node_kind::alternate},
    valid_case{"||", 5, node_kind::alternate},     valid_case{"a||b", 5, node_kind::alternate},
    valid_case{"(|a)b", 5, node_kind::concat},     valid_case{"[abc]", 1, node_kind::bytes},
    valid_case{"[^a]", 1, node_kind::bytes},       valid_case{R"(\x00\xFF)", 3, node_kind::concat},
    valid_case{R"(\x414)", 3, node_kind::concat},  valid_case{"a\0\xFF"sv, 5, node_kind::concat},
    valid_case{" /-\n\t", 9, node_kind::concat},
};

// These same fixtures are checked by static_assert and by the runtime runner.
constexpr bool valid_patterns() {
    for (const auto& fixture : valid_cases) {
        const auto parsed = parse<small>(fixture.pattern);
        if (!parsed || parsed.tree.size != fixture.nodes ||
            parsed.tree.nodes[parsed.tree.root].kind != fixture.root) {
            return false;
        }
        for (std::size_t id = 0; id < parsed.tree.size; ++id) {
            const auto& node = parsed.tree.nodes[id];
            if (node.span.begin > node.span.end || node.span.end > fixture.pattern.size() ||
                (node.left != no_node && node.left >= id) ||
                (node.right != no_node && node.right >= id) ||
                (node.kind == node_kind::bytes && node.set >= parsed.tree.set_count)) {
                return false;
            }
        }
    }
    return true;
}

struct invalid_case {
    std::string_view pattern;
    std::size_t offset;
};

constexpr std::array invalid_cases{
    invalid_case{"a)", 1},        invalid_case{"a]", 1},          invalid_case{"a+", 1},
    invalid_case{"(?=a)", 1},     invalid_case{"*a", 0},          invalid_case{"**", 0},
    invalid_case{"a**", 2},       invalid_case{"(", 1},           invalid_case{"((", 2},
    invalid_case{"a(b", 3},       invalid_case{"\\", 1},          invalid_case{"[", 1},
    invalid_case{"[a-", 3},       invalid_case{"[]", 1},          invalid_case{"[^]", 2},
    invalid_case{"[-a]", 1},      invalid_case{"[a-]", 3},        invalid_case{"[z-a]", 3},
    invalid_case{"[a-b-c]", 4},   invalid_case{"[a^]", 2},        invalid_case{"[a-^]", 3},
    invalid_case{"[a[b]]", 2},    invalid_case{"[[:alpha:]]", 1}, invalid_case{"[a--b]", 3},
    invalid_case{R"(\n)", 1},     invalid_case{R"(\*)", 1},       invalid_case{R"(\d)", 1},
    invalid_case{R"(\0)", 1},     invalid_case{R"(\1)", 1},       invalid_case{R"(\u0041)", 1},
    invalid_case{R"(\XFF)", 1},   invalid_case{R"(\xG0)", 2},     invalid_case{R"(\x0)", 3},
    invalid_case{R"(\x)", 2},     invalid_case{R"(\x0G)", 3},     invalid_case{R"(\x{41})", 2},
    invalid_case{R"([\xG0])", 3}, invalid_case{"[\\", 2},         invalid_case{".", 0},
    invalid_case{"^a", 0},        invalid_case{"a$", 1},          invalid_case{"a?", 1},
    invalid_case{"a{2}", 1},      invalid_case{"}", 0},           invalid_case{"a*?", 2},
    invalid_case{"a*+", 2},       invalid_case{"(?:a)", 1},       invalid_case{"(?<n>a)", 1},
    invalid_case{"(?i)", 1},      invalid_case{"a|*", 2},         invalid_case{"(*)", 1},
};

constexpr bool invalid_patterns() {
    for (const auto& fixture : invalid_cases) {
        const auto parsed = parse<small>(fixture.pattern);
        if (parsed || parsed.error != diagnostic{error_code::invalid_pattern, fixture.offset} ||
            parsed.tree.root != no_node || parsed.tree.size != 0 || parsed.tree.set_count != 0) {
            return false;
        }
    }
    return true;
}

struct set_case {
    std::string_view pattern;
    std::string_view members;
    bool complement = false;
};

constexpr std::array set_cases{
    set_case{"[abc]", "abc"},        set_case{"[aab]", "ab"},
    set_case{"[a-c]", "abc"},        set_case{"[a-a]", "a"},
    set_case{"[^a]", "a", true},     set_case{R"([\x7F-\x81])", "\x7F\x80\x81"sv},
    set_case{R"([a\x2Dz])", "a-z"},  set_case{R"([\x5B\x5D\x5E\x2D\x5C])", "[]^-\\"},
    set_case{R"([\x2D-0])", "-./0"}, set_case{"[.*+?()|${}]", ".*+?()|${}"},
    set_case{"[a&&b]", "a&b"},       set_case{"[\0\xFF]"sv, "\0\xFF"sv},
    set_case{R"(\x00)", "\0"sv},     set_case{R"(\xFF)", "\xFF"sv},
    set_case{R"(\xff)", "\xFF"sv},   set_case{R"(\x2A)", "*"},
    set_case{R"(\x7C)", "|"},        set_case{R"(\x5D)", "]"},
};

constexpr bool byte_sets() {
    for (const auto& fixture : set_cases) {
        const auto parsed = parse<small>(fixture.pattern);
        if (!parsed || parsed.tree.size != 1 || parsed.tree.set_count != 1) {
            return false;
        }
        for (unsigned value = 0; value < 256; ++value) {
            bool expected = false;
            for (const char member : fixture.members) {
                if (static_cast<unsigned char>(member) == value) {
                    expected = true;
                }
            }
            if (fixture.complement) {
                expected = !expected;
            }
            if (parsed.tree.sets[0].contains(static_cast<std::uint8_t>(value)) != expected) {
                return false;
            }
        }
    }
    const auto all = parse<small>(R"([\x00-\xFF])");
    const auto none = parse<small>(R"([^\x00-\xFF])");
    if (!all || !none) {
        return false;
    }
    for (unsigned value = 0; value < 256; ++value) {
        if (!all.tree.sets[0].contains(static_cast<std::uint8_t>(value)) ||
            none.tree.sets[0].contains(static_cast<std::uint8_t>(value))) {
            return false;
        }
    }
    return true;
}

constexpr bool structure_and_spans() {
    const auto parsed = parse<small>("ab|cd*");
    if (!parsed || parsed.tree.size != 8 || parsed.tree.root != 7) {
        return false;
    }
    const auto& n = parsed.tree.nodes;
    if (n[2].kind != node_kind::concat || n[2].left != 0 || n[2].right != 1 ||
        n[2].span != source_span{0, 2} || n[5].kind != node_kind::star || n[5].left != 4 ||
        n[5].span != source_span{4, 6} || n[6].kind != node_kind::concat || n[6].left != 3 ||
        n[6].right != 5 || n[7].kind != node_kind::alternate || n[7].left != 2 || n[7].right != 6 ||
        n[7].span != source_span{0, 6}) {
        return false;
    }
    const auto grouped = parse<small>("(a|)*b");
    const auto& g = grouped.tree.nodes;
    if (!grouped || g[0].span != source_span{1, 2} || g[1].span != source_span{3, 3} ||
        g[2].span != source_span{0, 4} || g[3].span != source_span{0, 5} ||
        g[grouped.tree.root].span != source_span{0, 6}) {
        return false;
    }
    const auto escaped = parse<small>(R"(\x00[\xFF])");
    if (!escaped || escaped.tree.nodes[0].span != source_span{0, 4} ||
        escaped.tree.nodes[1].span != source_span{4, 10}) {
        return false;
    }
    const auto concat = parse<small>("abc");
    const auto alternative = parse<small>("a|b|c");
    return concat && alternative && concat.tree.nodes[4].left == 2 &&
           concat.tree.nodes[4].right == 3 && alternative.tree.nodes[4].left == 2 &&
           alternative.tree.nodes[4].right == 3;
}

constexpr bool limits() {
    constexpr parser_limits three_nodes{.ast_nodes = 3};
    constexpr parser_limits two_nodes{.ast_nodes = 2};
    constexpr parser_limits zero_nodes{.ast_nodes = 0};
    constexpr parser_limits one_group{.nesting = 1, .ast_nodes = 8};
    constexpr parser_limits no_groups{.nesting = 0, .ast_nodes = 8};
    constexpr parser_limits four_bytes{.pattern_bytes = 4, .ast_nodes = 8};
    constexpr parser_limits three_bytes{.pattern_bytes = 3, .ast_nodes = 8};
    constexpr parser_limits no_bytes{.pattern_bytes = 0, .ast_nodes = 8};
    constexpr parser_limits no_work{.ast_nodes = 8, .work = 0};
    constexpr parser_limits one_work{.ast_nodes = 8, .work = 1};
    constexpr parser_limits literal_work{.ast_nodes = 8, .work = 16};
    constexpr parser_limits short_work{.ast_nodes = 8, .work = 15};
    return parse<three_nodes>("ab") &&
           parse<two_nodes>("ab").error == diagnostic{error_code::ast_nodes_exceeded, 0} &&
           parse<zero_nodes>("").error == diagnostic{error_code::ast_nodes_exceeded, 0} &&
           parse<one_group>("(a)") &&
           parse<one_group>("((a))").error == diagnostic{error_code::nesting_exceeded, 0} &&
           parse<no_groups>("a") &&
           parse<no_groups>("()").error == diagnostic{error_code::nesting_exceeded, 0} &&
           parse<four_bytes>(R"(\x00)") &&
           parse<three_bytes>(R"(\x00)").error ==
               diagnostic{error_code::pattern_bytes_exceeded, 0} &&
           parse<no_bytes>("") &&
           parse<no_bytes>("a").error == diagnostic{error_code::pattern_bytes_exceeded, 0} &&
           parse<no_work>("").error == diagnostic{error_code::work_exceeded, 0} &&
           parse<one_work>("") && parse<literal_work>("a") &&
           parse<literal_work>("a").work_used == 16 &&
           parse<short_work>("a").error == diagnostic{error_code::work_exceeded, 0};
}

constexpr bool binary_inputs() {
    constexpr parser_limits single{.ast_nodes = 1};
    constexpr std::string_view digits = "0123456789ABCDEF";
    for (unsigned value = 0; value < 256; ++value) {
        const std::array spelling{'\\', 'x', digits[value / 16], digits[value % 16]};
        const auto escaped = parse<single>(std::string_view{spelling.data(), spelling.size()});
        if (!escaped || escaped.tree.set_count != 1 ||
            !escaped.tree.sets[0].contains(static_cast<std::uint8_t>(value))) {
            return false;
        }
        // Control bytes and the entire high-byte range are ordinary literals.
        if (value < 32 || value >= 127) {
            const auto raw = std::bit_cast<char>(static_cast<std::uint8_t>(value));
            const auto parsed = parse<single>(std::string_view{&raw, 1});
            if (!parsed || parsed.tree.sets[0] != escaped.tree.sets[0]) {
                return false;
            }
        }
    }
    const auto literal = parse<small>("a\0\xFF");
    const auto view = parse<small>("a\0\xFF"sv);
    const auto empty = parse<small>(std::string_view{});
    return literal && view && empty && literal.tree.size == 5 && view.tree.size == 5 &&
           literal.tree.nodes[literal.tree.root].span == source_span{0, 3} &&
           literal.tree.sets[1].contains(0) && literal.tree.sets[2].contains(255) &&
           empty.tree.nodes[0].kind == node_kind::epsilon;
}

template <std::size_t Work> constexpr bool work_boundary(std::string_view pattern) {
    constexpr parser_limits exact{.ast_nodes = 16, .work = Work};
    constexpr parser_limits short_limit{.ast_nodes = 16, .work = Work - 1};
    const auto admitted = parse<exact>(pattern);
    const auto rejected = parse<short_limit>(pattern);
    return admitted && admitted.work_used == Work &&
           rejected.error == diagnostic{error_code::work_exceeded, 0} &&
           rejected.tree.root == no_node && rejected.tree.size == 0 && rejected.tree.set_count == 0;
}

constexpr bool work_boundaries() {
    return work_boundary<19>("a*") && work_boundary<20>("(a)") && work_boundary<35>("ab") &&
           work_boundary<70>("[a-z]") && work_boundary<79>("[^a-z]") && work_boundary<28>("(a|)*");
}

constexpr bool deep_groups() {
    std::array<char, 131> pattern{};
    for (std::size_t i = 0; i < 65; ++i) {
        pattern[i] = '(';
        pattern[130 - i] = ')';
    }
    pattern[65] = 'a';
    const auto exact = parse<small>(std::string_view{pattern.data() + 1, 129});
    const auto exceeded = parse<small>(std::string_view{pattern.data(), pattern.size()});
    return exact && exact.tree.size == 1 && exact.tree.nodes[0].span == source_span{0, 129} &&
           exceeded.error == diagnostic{error_code::nesting_exceeded, 0} &&
           exceeded.tree.root == no_node && exceeded.tree.size == 0;
}

template <std::size_t Capacity>
constexpr bool clean_failure(const parse_result<Capacity>& parsed, error_code code) {
    return !parsed && parsed.error == diagnostic{code, 0} && parsed.tree.root == no_node &&
           parsed.tree.size == 0 && parsed.tree.set_count == 0;
}

constexpr bool partial_failures() {
    constexpr parser_limits zero{.ast_nodes = 0};
    constexpr parser_limits one{.ast_nodes = 1};
    constexpr parser_limits two{.ast_nodes = 2};
    constexpr parser_limits range_work{.ast_nodes = 8, .work = 10};
    constexpr parser_limits star_work{.ast_nodes = 8, .work = 17};
    return parse<two>("a*") && clean_failure(parse<zero>("[a]"), error_code::ast_nodes_exceeded) &&
           clean_failure(parse<one>("a*"), error_code::ast_nodes_exceeded) &&
           clean_failure(parse<one>("|"), error_code::ast_nodes_exceeded) &&
           clean_failure(parse<two>("a|"), error_code::ast_nodes_exceeded) &&
           clean_failure(parse<one>("()()"), error_code::ast_nodes_exceeded) &&
           clean_failure(parse<range_work>("[a-z]"), error_code::work_exceeded) &&
           clean_failure(parse<star_work>("a*"), error_code::work_exceeded);
}

static_assert(valid_patterns());
static_assert(invalid_patterns());
static_assert(byte_sets());
static_assert(structure_and_spans());
static_assert(limits());
static_assert(binary_inputs());
static_assert(work_boundaries());
static_assert(deep_groups());
static_assert(partial_failures());

constexpr auto static_tree = parse<small>("ab|cd*");
static_assert(static_tree && static_tree.tree.root == 7);

bool dumps() {
    const auto parsed = parse<small>("ab|cd*");
    constexpr std::string_view expected = "0 bytes [0,1) {61}\n"
                                          "1 bytes [1,2) {62}\n"
                                          "2 concat [0,2) left=0 right=1\n"
                                          "3 bytes [3,4) {63}\n"
                                          "4 bytes [4,5) {64}\n"
                                          "5 star [4,6) left=4\n"
                                          "6 concat [3,6) left=3 right=5\n"
                                          "7 alternate [0,6) left=2 right=6\n"
                                          "root 7\n";
    return parsed && dump_ast(parsed.tree) == expected && dump_ast(static_tree.tree) == expected &&
           dump_ast(parse<small>("ab|cd*").tree) == expected &&
           dump_ast(parse<small>("").tree) == "0 epsilon [0,0)\nroot 0\n" &&
           dump_ast(parse<small>(R"([\x00-\xFF])").tree) == "0 bytes [0,11) {00-FF}\nroot 0\n" &&
           dump_ast(parse<small>(R"([^\x00-\xFF])").tree) == "0 bytes [0,12) {}\nroot 0\n" &&
           dump_ast(parse<small>("[ac]").tree) == "0 bytes [0,4) {61,63}\nroot 0\n" &&
           dump_ast(parse<small>("(").tree) == "empty\n";
}

bool full_size_inputs() {
    const std::string nested = std::string(64, '(') + "a" + std::string(64, ')');
    const auto exact = parse(nested);
    const auto too_deep = parse('(' + nested + ')');
    const auto long_concat = parse(std::string(4096, 'a'));
    const auto too_long = parse(std::string(4097, 'a'));
    const auto arena_full = parse(std::string(4095, '|'));
    const auto arena_over = parse(std::string(4096, '|'));
    return exact && exact.tree.size == 1 &&
           too_deep.error == diagnostic{error_code::nesting_exceeded, 0} && long_concat &&
           long_concat.tree.size == 8191 &&
           too_long.error == diagnostic{error_code::pattern_bytes_exceeded, 0} && arena_full &&
           arena_full.tree.size == 8191 &&
           arena_over.error == diagnostic{error_code::ast_nodes_exceeded, 0};
}

bool short_binary_patterns() {
    // Exercise every pair from a syntax-heavy alphabet, with no terminator in
    // the input span. This also checks failed trees under ASan/UBSan.
    constexpr std::string_view alphabet = "a|()*[]^-\\x0G?\0\xFF"sv;
    constexpr parser_limits bounded{.ast_nodes = 8};
    for (const char first : alphabet) {
        for (const char second : alphabet) {
            const std::array input{first, second};
            const auto parsed = parse<bounded>(std::string_view{input.data(), input.size()});
            if (!parsed) {
                if (parsed.error.code != error_code::invalid_pattern || parsed.error.offset > 2 ||
                    parsed.tree.root != no_node || parsed.tree.size != 0 ||
                    parsed.tree.set_count != 0) {
                    return false;
                }
                continue;
            }
            // "||" is the largest tree here: three epsilons and two alternations.
            if (parsed.tree.root >= parsed.tree.size || parsed.tree.size > 5) {
                return false;
            }
            for (std::size_t id = 0; id < parsed.tree.size; ++id) {
                const auto& node = parsed.tree.nodes[id];
                if (node.span.begin > node.span.end || node.span.end > input.size() ||
                    (node.left != no_node && node.left >= id) ||
                    (node.right != no_node && node.right >= id)) {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace

int main() {
    // Function pointers prevent these calls from being manifestly constant evaluated.
    const std::array tests{std::pair{"valid patterns", &valid_patterns},
                           std::pair{"invalid patterns", &invalid_patterns},
                           std::pair{"byte sets", &byte_sets},
                           std::pair{"structure and spans", &structure_and_spans},
                           std::pair{"limits", &limits},
                           std::pair{"binary inputs", &binary_inputs},
                           std::pair{"work boundaries", &work_boundaries},
                           std::pair{"deep groups", &deep_groups},
                           std::pair{"partial failures", &partial_failures},
                           std::pair{"dumps", &dumps},
                           std::pair{"full size inputs", &full_size_inputs},
                           std::pair{"short binary patterns", &short_binary_patterns}};
    for (const auto& [name, run] : tests) {
        if (!run()) {
            std::cerr << "FAILED: " << name << '\n';
            return 1;
        }
    }
    return 0;
}
