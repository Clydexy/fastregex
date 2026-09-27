# Fastregex

A C++ compile-time DFA regex engine under development. This initial scaffold
provides build targets and a test harness; regex compilation and matching are
not implemented yet. The proposed architecture and milestone plan live in the
[Linear project](https://linear.app/compile-time-regex/project/fastregex-compile-time-dfa-engine-6b3db65da5ba).

Contributor instructions and pre-commit/pre-push checks are in
[AGENTS.md](AGENTS.md). Static analysis uses `.clang-tidy`; formatting uses
`.clang-format`. These are development tools, not consumer dependencies.

## Prerequisites

- CMake 3.25 or newer.
- Ninja for the checked-in presets.
- A C++23 compiler. The initial development baseline is Apple Clang 17
  or newer on macOS arm64 (validated with Apple Clang 17 and 21). Other compilers
  are not yet validated; future compiler components may need additional C++23
  library support. The scaffold tests `if consteval` in both evaluation modes.

On macOS, install Xcode or the Command Line Tools (`xcode-select --install`).
If tools report an unaccepted Xcode license, run `sudo xcodebuild -license`
in a terminal and review and accept the agreement before building. A full Xcode
installation may also require `sudo xcodebuild -runFirstLaunch`.
Use `xcode-select -p` to inspect the selected developer directory.
Install CMake and Ninja separately (for example, `brew install cmake ninja`).

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
presets run a public-header smoke test and a C++23 compiler-target smoke test.
Failures return nonzero even with Release assertions disabled. No test framework
download is required. Add component tests under `tests/` with each implementation;
these smoke tests do not replace the milestone's end-to-end tests.

To select another compiler, set `CXX` before the first configure, or create an
ignored `CMakeUserPresets.json`. Remove the affected build directory before
changing compiler in an existing configuration.

## Consumer integration

```cmake
add_subdirectory(path/to/fastregex)
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE fastregex::fastregex)
```

The public interface currently exposes only `<fastregex/version.hpp>` and needs
C++11. Compiler development uses the separate, internal `fastregex_compiler`
target and C++23. Linking the public target does not expose `src/` or impose the
compiler's language requirement. Installed packages, generated pattern helpers,
and matching APIs are later work.

`FASTREGEX_BUILD_TESTS` defaults to ON for a standalone build and OFF when used
via `add_subdirectory` or FetchContent. `FASTREGEX_BUILD_BENCHMARKS` defaults to
OFF everywhere; its directory is currently a placeholder. Explicitly disable
both for a minimal standalone configure:

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
| `examples/` | Future runnable consumer examples |
