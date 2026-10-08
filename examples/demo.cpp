#include <fastregex/regex.hpp>

#include <iostream>
#include <string_view>

// The pattern is compiled into a DFA at C++ compile time. The same API can
// evaluate a known subject at compile time or match runtime input below.
static_assert(fastregex::full_match<"a(b|c)*">("abcb"));

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [input]\n";
        return 2;
    }

    const std::string_view input = argc > 1 ? argv[1] : "abcb";
    const bool matched = fastregex::full_match<"a(b|c)*">(input);

    std::cout << "Pattern: a(b|c)*\n"
              << "Input: " << input << '\n'
              << "Result: " << (matched ? "MATCH" : "NO MATCH") << '\n';
    return matched ? 0 : 1;
}
