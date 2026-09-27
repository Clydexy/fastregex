#pragma once

#include "compiler/dfa.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace fastregex::compiler {

namespace detail {

template <std::size_t StateCapacity, typename ReadByte>
constexpr bool full_match_impl(const dfa<StateCapacity>& machine, std::size_t length,
                               ReadByte read_byte) noexcept {
    if (machine.start >= machine.state_count || machine.sink >= machine.state_count ||
        machine.start == machine.sink) {
        return false;
    }

    auto state = machine.start;
    for (std::size_t offset = 0; offset < length; ++offset) {
        const auto next = machine.states[state].transitions[read_byte(offset)];
        if (next == machine.sink) {
            return false;
        }
        if (next >= machine.state_count) {
            return false;
        }
        state = next;
    }
    return machine.states[state].accepting;
}

} // namespace detail

template <std::size_t StateCapacity>
constexpr bool full_match(const dfa<StateCapacity>& machine, const std::uint8_t* subject,
                          std::size_t length) noexcept {
    if (subject == nullptr && length != 0) {
        return false;
    }
    return detail::full_match_impl(machine, length,
                                   [subject](std::size_t offset) { return subject[offset]; });
}

template <std::size_t StateCapacity>
constexpr bool full_match(const dfa<StateCapacity>& machine,
                          std::span<const std::uint8_t> subject) noexcept {
    return full_match(machine, subject.data(), subject.size());
}

template <std::size_t StateCapacity>
constexpr bool full_match(const dfa<StateCapacity>& machine, std::string_view subject) noexcept {
    return detail::full_match_impl(machine, subject.size(), [subject](std::size_t offset) {
        return std::bit_cast<std::uint8_t>(subject[offset]);
    });
}

} // namespace fastregex::compiler
