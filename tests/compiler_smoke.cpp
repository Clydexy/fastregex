#include <fastregex/version.hpp>

// Exercise a C++23 language feature and both constant and runtime evaluation.
// Regex component tests will be added alongside their implementations.
constexpr int evaluation_mode() {
    if consteval {
        return 23;
    } else {
        return 0;
    }
}

static_assert(evaluation_mode() == 23);
static_assert(fastregex::version_major == 0);

int main() {
    return evaluation_mode();
}
