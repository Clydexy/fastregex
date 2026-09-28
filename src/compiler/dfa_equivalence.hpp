#pragma once

#include "compiler/dfa.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fastregex::compiler {

struct equivalence_limits {
    std::size_t pairs = 4'096;
    std::size_t work = 16'777'216;
};

template <std::size_t PairCapacity> struct equivalence_result {
    std::array<std::uint8_t, PairCapacity> counterexample{};
    std::size_t counterexample_size = 0;
    std::size_t pair_count = 0;
    std::size_t work_used = 0;
    diagnostic error{};
    bool equivalent = false;

    constexpr explicit operator bool() const {
        return error.code == error_code::none;
    }
};

namespace detail {

struct dfa_state_pair {
    dfa_state_id left = no_dfa_state;
    dfa_state_id right = no_dfa_state;

    constexpr bool operator==(const dfa_state_pair&) const = default;
};

template <equivalence_limits Limits, std::size_t LeftCapacity, std::size_t RightCapacity>
class equivalence_checker {
    static_assert(Limits.pairs > 0);

    static constexpr std::size_t no_predecessor = std::numeric_limits<std::size_t>::max();

    const dfa<LeftCapacity>& left_;
    const dfa<RightCapacity>& right_;
    equivalence_result<Limits.pairs>& result_;
    std::array<dfa_state_pair, Limits.pairs> pairs_{};
    std::array<std::size_t, Limits.pairs> predecessors_{};
    std::array<std::uint8_t, Limits.pairs> predecessor_bytes_{};

    constexpr void fail(error_code code) {
        if (result_) {
            result_.error = {code, 0};
            result_.equivalent = false;
            result_.counterexample_size = 0;
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

    template <std::size_t StateCapacity>
    constexpr bool valid_machine(const dfa<StateCapacity>& machine) {
        if (!charge()) {
            return false;
        }
        if (machine.state_count == 0 || machine.state_count > StateCapacity ||
            machine.start >= machine.state_count || machine.sink >= machine.state_count ||
            machine.states[machine.sink].accepting || machine.byte_class_count == 0 ||
            machine.byte_class_count > byte_alphabet_size) {
            return false;
        }
        for (std::size_t byte = 0; result_ && byte < byte_alphabet_size; ++byte) {
            if (!charge()) {
                return false;
            }
            if (machine.byte_classes[byte] >= machine.byte_class_count) {
                return false;
            }
        }
        for (std::size_t byte_class = 0; result_ && byte_class < machine.byte_class_count;
             ++byte_class) {
            if (!charge()) {
                return false;
            }
            if (machine.states[machine.sink].transitions[byte_class] != machine.sink) {
                return false;
            }
        }
        return static_cast<bool>(result_);
    }

    constexpr bool acceptance_differs(const dfa_state_pair& pair) {
        return left_.states[pair.left].accepting != right_.states[pair.right].accepting;
    }

    constexpr std::size_t find_pair(const dfa_state_pair& pair) {
        for (std::size_t index = 0; result_ && index < result_.pair_count; ++index) {
            if (!charge()) {
                break;
            }
            if (pairs_[index] == pair) {
                return index;
            }
        }
        return no_predecessor;
    }

    constexpr std::size_t add_pair(const dfa_state_pair& pair, std::size_t predecessor,
                                   std::uint8_t byte) {
        if (!charge()) {
            return no_predecessor;
        }
        if (result_.pair_count == Limits.pairs) {
            fail(error_code::work_exceeded);
            return no_predecessor;
        }
        const auto index = result_.pair_count++;
        pairs_[index] = pair;
        predecessors_[index] = predecessor;
        predecessor_bytes_[index] = byte;
        return index;
    }

    constexpr void build_counterexample(std::size_t mismatch) {
        std::size_t length = 0;
        for (auto index = mismatch; predecessors_[index] != no_predecessor;
             index = predecessors_[index]) {
            if (!charge()) {
                return;
            }
            result_.counterexample[length++] = predecessor_bytes_[index];
        }
        for (std::size_t first = 0; first < length / 2; ++first) {
            if (!charge()) {
                return;
            }
            const auto last = length - first - 1;
            const auto byte = result_.counterexample[first];
            result_.counterexample[first] = result_.counterexample[last];
            result_.counterexample[last] = byte;
        }
        result_.counterexample_size = length;
    }

  public:
    constexpr equivalence_checker(const dfa<LeftCapacity>& left, const dfa<RightCapacity>& right,
                                  equivalence_result<Limits.pairs>& result)
        : left_(left), right_(right), result_(result) {}

    constexpr void run() {
        if (!valid_machine(left_) || !valid_machine(right_)) {
            if (result_) {
                fail(error_code::invalid_pattern);
            }
            return;
        }

        const auto initial = add_pair({left_.start, right_.start}, no_predecessor, 0);
        if (!result_) {
            return;
        }
        if (acceptance_differs(pairs_[initial])) {
            build_counterexample(initial);
            return;
        }

        for (std::size_t head = 0; result_ && head < result_.pair_count; ++head) {
            for (std::size_t byte = 0; result_ && byte < byte_alphabet_size; ++byte) {
                if (!charge()) {
                    break;
                }
                const dfa_state_pair next{
                    transition_for_byte(left_, pairs_[head].left, static_cast<std::uint8_t>(byte)),
                    transition_for_byte(right_, pairs_[head].right,
                                        static_cast<std::uint8_t>(byte))};
                if (next.left >= left_.state_count || next.right >= right_.state_count) {
                    fail(error_code::invalid_pattern);
                    break;
                }
                const auto existing = find_pair(next);
                if (!result_ || existing != no_predecessor) {
                    continue;
                }
                const auto index = add_pair(next, head, static_cast<std::uint8_t>(byte));
                if (result_ && acceptance_differs(next)) {
                    build_counterexample(index);
                    return;
                }
            }
        }
        if (result_) {
            result_.equivalent = true;
        }
    }
};

} // namespace detail

template <equivalence_limits Limits = equivalence_limits{}, std::size_t LeftCapacity,
          std::size_t RightCapacity>
constexpr equivalence_result<Limits.pairs> equivalent_dfas(const dfa<LeftCapacity>& left,
                                                           const dfa<RightCapacity>& right) {
    equivalence_result<Limits.pairs> result;
    detail::equivalence_checker<Limits, LeftCapacity, RightCapacity>{left, right, result}.run();
    return result;
}

} // namespace fastregex::compiler
