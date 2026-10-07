# Fastregex

A C++ compile-time DFA regex engine under development. Includes a constexpr
byte-regex parser, bounded Thompson NFA and DFA construction, a portable full-match
executor, and an enforced compile-time pattern API.

See the [language specification](docs/byte-regex.md) for syntax, examples,
diagnostics and compilation limits.

For a runnable demo, see the [examples guide](examples/README.md).

Static analysis uses `.clang-tidy`; formatting uses `.clang-format`.
These are development tools, not consumer dependencies.

## Prerequisites

- CMake 3.25 or newer.
- Ninja for the checked-in presets.
- A C++23 compiler. The initial development baseline is Apple Clang 17
  or newer on macOS arm64 (validated with Apple Clang 17 and 21). Other compilers
  are not yet validated; future compiler components may need additional C++23
  library support. The scaffold tests `if consteval` in both evaluation modes.

## Configure, build and test

From a clean checkout at the repository root:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

cmake --preset release
cmake --build --preset release
ctest --preset release
```

Build outputs are separated under `build/debug` and `build/release`. Both
presets run public-header and compiler smoke tests plus parser, NFA, DFA and static API tests.
Failures return nonzero even with Release assertions disabled. No test framework
download is required. Add component tests under `tests/` with each implementation.

The Phase 1 regression test runs the same operator and boundary-byte corpus through
constant evaluation and the runtime compiler pipeline. Run it alone with:

```sh
ctest --preset debug -R fastregex.phase1_regression --output-on-failure
```

To select another compiler, set `CXX` before the first configure, or create an
ignored `CMakeUserPresets.json`. Remove the affected build directory before
changing compiler in an existing configuration.

## Consumer integration

```cmake
add_subdirectory(path/to/fastregex)
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE fastregex::compile_time)
```

```cpp
#include <fastregex/regex.hpp>

static_assert(fastregex::full_match<"a(b|c)*">("abcb"));
bool matched = fastregex::full_match<"[a-c]*">(input);
```

| Target | Requirement | Purpose |
| --- | --- | --- |
| `fastregex::compile_time` | C++23 | Compile fixed-string patterns and run full matches |
| `fastregex::fastregex` | C++11 | Version header only |

Each template pattern is compiled by a `consteval` pipeline into an exact-capacity
DFA. Calls execute that artifact and never parse a pattern at runtime. Invalid
patterns fail compilation with a named error code; the diagnostic's
`compile_failure<code, offset>` shows the source-byte offset. Limits are listed in
the [language specification](docs/byte-regex.md#compilation-limits).

`FASTREGEX_BUILD_TESTS` defaults to ON for a standalone build and OFF when used
via `add_subdirectory` or FetchContent. `FASTREGEX_BUILD_BENCHMARKS` defaults to
OFF everywhere; its directory is currently a placeholder. `FASTREGEX_BUILD_EXAMPLES`
defaults to OFF; enable it to build `fastregex_demo`. Explicitly disable
tests and benchmarks for a minimal standalone configure:

```sh
cmake -S . -B build/library -G Ninja \
  -DFASTREGEX_BUILD_TESTS=OFF -DFASTREGEX_BUILD_BENCHMARKS=OFF
cmake --build build/library
```

## Layout

| Directory | Purpose |
| --- | --- |
| `include/fastregex/` | Consumer-facing headers |
| `src/compiler/` | Private C++23 compiler implementation |
| `tests/` | CTest smoke and future component tests |
| `tools/` | Future host emitters and developer utilities |
| `benchmarks/` | Optional performance measurements |
| `examples/` | Runnable consumer demo and build instructions |
| `docs/` | Normative language and implementation contracts |
