#pragma once

#include "compiler/dfa.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fastregex::compiler {

struct optimization_limits {
    std::size_t work = 16'777'216;
};

struct dfa_statistics {
    std::size_t states = 0;
    std::size_t byte_classes = 0;
    std::size_t table_entries = 0;

    constexpr bool operator==(const dfa_statistics&) const = default;
};

struct optimization_statistics {
    dfa_statistics input{};
    dfa_statistics after_byte_classes{};
    dfa_statistics after_unreachable_removal{};
    dfa_statistics after_minimization{};
    dfa_statistics output{};

    constexpr bool operator==(const optimization_statistics&) const = default;
};

template <std::size_t StateCapacity> struct dfa_pass_result {
    dfa<StateCapacity> machine{};
    diagnostic error{};
    std::size_t work_used = 0;

    constexpr explicit operator bool() const {
        return error.code == error_code::none;
    }
};

template <std::size_t StateCapacity> struct optimize_result {
    dfa<StateCapacity> machine{};
    optimization_statistics statistics{};
    diagnostic error{};
    std::size_t work_used = 0;

    constexpr explicit operator bool() const {
        return error.code == error_code::none;
    }
};

template <std::size_t StateCapacity>
constexpr dfa_statistics statistics_for(const dfa<StateCapacity>& machine) noexcept {
    return {.states = machine.state_count,
            .byte_classes = machine.byte_class_count,
            .table_entries = machine.state_count * machine.byte_class_count};
}

namespace detail {

template <optimization_limits Limits> class optimization_context {
    diagnostic& error_;
    std::size_t& work_used_;

  public:
    constexpr optimization_context(diagnostic& error, std::size_t& work_used)
        : error_(error), work_used_(work_used) {}

    constexpr explicit operator bool() const {
        return error_.code == error_code::none;
    }

    constexpr void fail(error_code code) {
        if (*this) {
            error_ = {code, 0};
        }
    }

    constexpr bool charge(std::size_t amount = 1) {
        if (!*this) {
            return false;
        }
        if (amount > Limits.work - work_used_) {
            fail(error_code::work_exceeded);
            return false;
        }
        work_used_ += amount;
        return true;
    }
};

template <std::size_t StateCapacity, optimization_limits Limits>
constexpr bool valid_dfa(const dfa<StateCapacity>& machine, optimization_context<Limits>& context) {
    if (!context.charge() || machine.state_count == 0 || machine.state_count > StateCapacity ||
        machine.start >= machine.state_count || machine.sink >= machine.state_count ||
        machine.states[machine.sink].accepting || machine.byte_class_count == 0 ||
        machine.byte_class_count > byte_alphabet_size) {
        if (context) {
            context.fail(error_code::invalid_pattern);
        }
        return false;
    }
    for (std::size_t byte = 0; context && byte < byte_alphabet_size; ++byte) {
        if (!context.charge()) {
            break;
        }
        if (machine.byte_classes[byte] >= machine.byte_class_count) {
            context.fail(error_code::invalid_pattern);
            break;
        }
    }
    for (std::size_t state = 0; context && state < machine.state_count; ++state) {
        for (std::size_t byte_class = 0; context && byte_class < machine.byte_class_count;
             ++byte_class) {
            if (!context.charge()) {
                break;
            }
            if (machine.states[state].transitions[byte_class] >= machine.state_count) {
                context.fail(error_code::invalid_pattern);
                break;
            }
            if (state == machine.sink &&
                machine.states[state].transitions[byte_class] != machine.sink) {
                context.fail(error_code::invalid_pattern);
                break;
            }
        }
    }
    return static_cast<bool>(context);
}

template <std::size_t StateCapacity, optimization_limits Limits>
constexpr bool same_byte_behavior(const dfa<StateCapacity>& machine, std::uint8_t left,
                                  std::uint8_t right, optimization_context<Limits>& context) {
    for (std::size_t state = 0; context && state < machine.state_count; ++state) {
        if (!context.charge()) {
            return false;
        }
        if (transition_for_byte(machine, static_cast<dfa_state_id>(state), left) !=
            transition_for_byte(machine, static_cast<dfa_state_id>(state), right)) {
            return false;
        }
    }
    return static_cast<bool>(context);
}

template <std::size_t StateCapacity, optimization_limits Limits>
constexpr dfa<StateCapacity> merge_byte_classes_impl(const dfa<StateCapacity>& input,
                                                     optimization_context<Limits>& context) {
    dfa<StateCapacity> output;
    output.state_count = input.state_count;
    output.start = input.start;
    output.sink = input.sink;

    std::array<std::uint8_t, byte_alphabet_size> representatives{};
    for (std::size_t byte = 0; context && byte < byte_alphabet_size; ++byte) {
        std::size_t byte_class = 0;
        for (; context && byte_class < output.byte_class_count; ++byte_class) {
            if (same_byte_behavior(input, static_cast<std::uint8_t>(byte),
                                   representatives[byte_class], context)) {
                break;
            }
        }
        if (!context) {
            break;
        }
        if (byte_class == output.byte_class_count) {
            representatives[output.byte_class_count++] = static_cast<std::uint8_t>(byte);
        }
        output.byte_classes[byte] = static_cast<std::uint8_t>(byte_class);
    }

    for (std::size_t state = 0; context && state < input.state_count; ++state) {
        if (!context.charge()) {
            break;
        }
        output.states[state].accepting = input.states[state].accepting;
        for (std::size_t byte_class = 0; context && byte_class < output.byte_class_count;
             ++byte_class) {
            if (!context.charge()) {
                break;
            }
            output.states[state].transitions[byte_class] = transition_for_byte(
                input, static_cast<dfa_state_id>(state), representatives[byte_class]);
        }
    }
    output.transition_count = output.state_count * output.byte_class_count;
    return context ? output : dfa<StateCapacity>{};
}

template <std::size_t StateCapacity, optimization_limits Limits>
constexpr dfa<StateCapacity> remove_unreachable_impl(const dfa<StateCapacity>& input,
                                                     optimization_context<Limits>& context) {
    std::array<bool, StateCapacity> reachable{};
    std::array<dfa_state_id, StateCapacity> worklist{};
    std::size_t head = 0;
    std::size_t tail = 0;
    reachable[input.start] = true;
    worklist[tail++] = input.start;
    while (context && head < tail) {
        const auto state = worklist[head++];
        for (std::size_t byte_class = 0; context && byte_class < input.byte_class_count;
             ++byte_class) {
            if (!context.charge()) {
                break;
            }
            const auto target = input.states[state].transitions[byte_class];
            if (!reachable[target]) {
                reachable[target] = true;
                worklist[tail++] = target;
            }
        }
    }
    reachable[input.sink] = true;

    dfa<StateCapacity> output;
    output.byte_classes = input.byte_classes;
    output.byte_class_count = input.byte_class_count;
    output.sink = 0;
    std::array<dfa_state_id, StateCapacity> remap{};
    remap.fill(no_dfa_state);
    remap[input.sink] = 0;
    output.state_count = 1;
    for (std::size_t state = 0; context && state < input.state_count; ++state) {
        if (!context.charge()) {
            break;
        }
        if (state != input.sink && reachable[state]) {
            remap[state] = static_cast<dfa_state_id>(output.state_count++);
        }
    }
    output.start = remap[input.start];

    for (std::size_t old_state = 0; context && old_state < input.state_count; ++old_state) {
        if (remap[old_state] == no_dfa_state) {
            continue;
        }
        if (!context.charge()) {
            break;
        }
        auto& state = output.states[remap[old_state]];
        state.accepting = input.states[old_state].accepting;
        for (std::size_t byte_class = 0; context && byte_class < input.byte_class_count;
             ++byte_class) {
            if (!context.charge()) {
                break;
            }
            state.transitions[byte_class] = remap[input.states[old_state].transitions[byte_class]];
        }
    }
    output.transition_count = output.state_count * output.byte_class_count;
    return context ? output : dfa<StateCapacity>{};
}

template <std::size_t StateCapacity, optimization_limits Limits>
constexpr bool same_partition_signature(const dfa<StateCapacity>& machine, std::size_t left,
                                        std::size_t right,
                                        const std::array<dfa_state_id, StateCapacity>& partition,
                                        optimization_context<Limits>& context) {
    if (!context.charge()) {
        return false;
    }
    if (partition[left] != partition[right] ||
        machine.states[left].accepting != machine.states[right].accepting) {
        return false;
    }
    for (std::size_t byte_class = 0; context && byte_class < machine.byte_class_count;
         ++byte_class) {
        if (!context.charge()) {
            return false;
        }
        const auto left_target = machine.states[left].transitions[byte_class];
        const auto right_target = machine.states[right].transitions[byte_class];
        if (partition[left_target] != partition[right_target]) {
            return false;
        }
    }
    return static_cast<bool>(context);
}

template <std::size_t StateCapacity, optimization_limits Limits>
constexpr dfa<StateCapacity> minimize_impl(const dfa<StateCapacity>& input,
                                           optimization_context<Limits>& context) {
    std::array<dfa_state_id, StateCapacity> partition{};
    for (std::size_t state = 0; context && state < input.state_count; ++state) {
        if (!context.charge()) {
            break;
        }
        partition[state] = input.states[state].accepting ? 1 : 0;
    }

    std::array<dfa_state_id, StateCapacity> refined{};
    std::array<dfa_state_id, StateCapacity> representatives{};
    std::size_t partition_count = 0;
    bool changed = true;
    while (context && changed) {
        partition_count = 0;
        for (std::size_t state = 0; context && state < input.state_count; ++state) {
            std::size_t candidate = 0;
            for (; context && candidate < partition_count; ++candidate) {
                if (same_partition_signature(input, state, representatives[candidate], partition,
                                             context)) {
                    break;
                }
            }
            if (!context) {
                break;
            }
            if (candidate == partition_count) {
                representatives[partition_count++] = static_cast<dfa_state_id>(state);
            }
            refined[state] = static_cast<dfa_state_id>(candidate);
        }
        changed = false;
        for (std::size_t state = 0; context && state < input.state_count; ++state) {
            if (!context.charge()) {
                break;
            }
            changed = changed || refined[state] != partition[state];
            partition[state] = refined[state];
        }
    }

    dfa<StateCapacity> output;
    output.byte_classes = input.byte_classes;
    output.byte_class_count = input.byte_class_count;
    output.state_count = partition_count;
    std::array<dfa_state_id, StateCapacity> partition_to_state{};
    partition_to_state.fill(no_dfa_state);
    const auto sink_partition = partition[input.sink];
    partition_to_state[sink_partition] = 0;
    std::size_t next_state = 1;
    for (std::size_t group = 0; context && group < partition_count; ++group) {
        if (!context.charge()) {
            break;
        }
        if (group != sink_partition) {
            partition_to_state[group] = static_cast<dfa_state_id>(next_state++);
        }
    }
    output.sink = 0;
    output.start = partition_to_state[partition[input.start]];
    for (std::size_t group = 0; context && group < partition_count; ++group) {
        if (!context.charge()) {
            break;
        }
        const auto source = partition_to_state[group];
        const auto representative = representatives[group];
        output.states[source].accepting = input.states[representative].accepting;
        for (std::size_t byte_class = 0; context && byte_class < input.byte_class_count;
             ++byte_class) {
            if (!context.charge()) {
                break;
            }
            const auto target = input.states[representative].transitions[byte_class];
            output.states[source].transitions[byte_class] = partition_to_state[partition[target]];
        }
    }
    output.transition_count = output.state_count * output.byte_class_count;
    return context ? output : dfa<StateCapacity>{};
}

} // namespace detail

template <optimization_limits Limits = optimization_limits{}, std::size_t StateCapacity>
constexpr dfa_pass_result<StateCapacity> merge_byte_classes(const dfa<StateCapacity>& machine) {
    dfa_pass_result<StateCapacity> result;
    detail::optimization_context<Limits> context{result.error, result.work_used};
    if (detail::valid_dfa(machine, context)) {
        result.machine = detail::merge_byte_classes_impl(machine, context);
    }
    return result;
}

template <optimization_limits Limits = optimization_limits{}, std::size_t StateCapacity>
constexpr dfa_pass_result<StateCapacity>
    remove_unreachable_states(const dfa<StateCapacity>& machine) {
    dfa_pass_result<StateCapacity> result;
    detail::optimization_context<Limits> context{result.error, result.work_used};
    if (detail::valid_dfa(machine, context)) {
        result.machine = detail::remove_unreachable_impl(machine, context);
    }
    return result;
}

template <optimization_limits Limits = optimization_limits{}, std::size_t StateCapacity>
constexpr dfa_pass_result<StateCapacity> minimize_dfa(const dfa<StateCapacity>& machine) {
    dfa_pass_result<StateCapacity> result;
    detail::optimization_context<Limits> context{result.error, result.work_used};
    if (detail::valid_dfa(machine, context)) {
        result.machine = detail::minimize_impl(machine, context);
    }
    return result;
}

template <optimization_limits Limits = optimization_limits{}, std::size_t StateCapacity>
constexpr optimize_result<StateCapacity> optimize_dfa(const dfa<StateCapacity>& machine) {
    optimize_result<StateCapacity> result;
    result.statistics.input = statistics_for(machine);
    detail::optimization_context<Limits> context{result.error, result.work_used};
    if (!detail::valid_dfa(machine, context)) {
        return result;
    }
    auto current = detail::merge_byte_classes_impl(machine, context);
    result.statistics.after_byte_classes = statistics_for(current);
    if (!context) {
        return result;
    }
    current = detail::remove_unreachable_impl(current, context);
    result.statistics.after_unreachable_removal = statistics_for(current);
    if (!context) {
        return result;
    }
    current = detail::minimize_impl(current, context);
    result.statistics.after_minimization = statistics_for(current);
    if (!context) {
        return result;
    }
    current = detail::merge_byte_classes_impl(current, context);
    result.statistics.output = statistics_for(current);
    if (context) {
        result.machine = current;
    }
    return result;
}

} // namespace fastregex::compiler
