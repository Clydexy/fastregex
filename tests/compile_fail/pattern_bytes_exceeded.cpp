#include <fastregex/regex.hpp>

consteval auto oversized_pattern() {
    fastregex::fixed_string<4'098> pattern;
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        pattern.value[index] = 'a';
    }
    pattern.value[pattern.size()] = '\0';
    return pattern;
}

constexpr bool result = fastregex::full_match<oversized_pattern()>("");

int main() {
    return result ? 0 : 1;
}
