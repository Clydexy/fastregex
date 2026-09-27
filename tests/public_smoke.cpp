#include <fastregex/version.hpp>

static_assert(fastregex::version_major == 0, "unexpected major version");
static_assert(fastregex::version_minor == 1, "unexpected minor version");
static_assert(fastregex::version_patch == 0, "unexpected patch version");

int main() {
    // Explicit return checks stay active in Release builds too.
    return fastregex::version_minor == 1 ? 0 : 1;
}
