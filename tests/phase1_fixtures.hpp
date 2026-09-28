#pragma once

#include <fastregex/regex.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fastregex::test {

using namespace std::string_view_literals;

struct match_case {
    std::string_view text;
    bool expected;
};

constexpr std::array<char, 2> nul_ff{0, std::bit_cast<char>(std::uint8_t{0xFF})};
constexpr std::array<char, 2> boundary_match{0, std::bit_cast<char>(std::uint8_t{0x7F})};
constexpr std::array<char, 2> boundary_miss{0, std::bit_cast<char>(std::uint8_t{0x7E})};

consteval auto make_all_bytes() {
    std::array<char, 256> bytes{};
    for (std::size_t value = 0; value < bytes.size(); ++value) {
        bytes[value] = std::bit_cast<char>(static_cast<std::uint8_t>(value));
    }
    return bytes;
}

constexpr auto all_bytes = make_all_bytes();

constexpr std::array concat_cases{match_case{"ab", true}, match_case{"", false},
                                  match_case{"a", false}, match_case{"abc", false}};
constexpr std::array alternate_cases{match_case{"a", true}, match_case{"bc", true},
                                     match_case{"", false}, match_case{"b", false},
                                     match_case{"abc", false}};
constexpr std::array star_cases{match_case{"a", true},    match_case{"ab", true},
                                match_case{"abbb", true}, match_case{"", false},
                                match_case{"b", false},   match_case{"aba", false}};
constexpr std::array group_cases{match_case{"a", true},   match_case{"ab", true},
                                 match_case{"ac", true},  match_case{"abcbcc", true},
                                 match_case{"", false},   match_case{"b", false},
                                 match_case{"abx", false}};
constexpr std::array class_cases{match_case{"az", true},
                                 match_case{"b\0"sv, true},
                                 match_case{std::string_view{nul_ff.data() + 1, 1}, false},
                                 match_case{"ax", false},
                                 match_case{"dx", false},
                                 match_case{"a", false}};
constexpr std::array complement_cases{match_case{"b", true}, match_case{"\0"sv, true},
                                      match_case{std::string_view{nul_ff.data() + 1, 1}, true},
                                      match_case{"a", false}, match_case{"", false}};
constexpr std::array precedence_cases{match_case{"ab", true},  match_case{"c", true},
                                      match_case{"cd", true},  match_case{"cddd", true},
                                      match_case{"", false},   match_case{"abc", false},
                                      match_case{"acd", false}};
constexpr std::array nullable_cycle_cases{match_case{"", true}, match_case{"a", true},
                                          match_case{"aaaa", true}, match_case{"b", false},
                                          match_case{"ab", false}};
constexpr std::array boundary_cases{
    match_case{std::string_view{nul_ff.data(), nul_ff.size()}, true},
    match_case{std::string_view{boundary_match.data(), boundary_match.size()}, true},
    match_case{std::string_view{boundary_miss.data(), boundary_miss.size()}, false},
    match_case{"\0"sv, false}, match_case{std::string_view{nul_ff.data() + 1, 1}, false}};
constexpr std::array alphabet_cases{
    match_case{"", true}, match_case{std::string_view{all_bytes.data(), all_bytes.size()}, true}};

template <typename Visitor> constexpr bool for_each_match_group(Visitor&& visit) {
    return visit.template operator()<"ab">(concat_cases) &&
           visit.template operator()<"a|bc">(alternate_cases) &&
           visit.template operator()<"ab*">(star_cases) &&
           visit.template operator()<"a(b|c)*">(group_cases) &&
           visit.template operator()<"[a-c][^x]">(class_cases) &&
           visit.template operator()<"[^a]">(complement_cases) &&
           visit.template operator()<"ab|cd*">(precedence_cases) &&
           visit.template operator()<"(|a*)*">(nullable_cycle_cases) &&
           visit.template operator()<R"(\x00[\x7F-\xFF])">(boundary_cases) &&
           visit.template operator()<R"([\x00-\xFF]*)">(alphabet_cases);
}

} // namespace fastregex::test
