#include <fastregex/regex.hpp>

constexpr bool result = fastregex::full_match<"a**">("a");

int main() {
    return result ? 0 : 1;
}
