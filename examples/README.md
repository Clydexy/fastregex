# Examples

`demo.cpp` matches a command-line input against the compile-time pattern
`a(b|c)*`: an `a`, followed by zero or more `b` or `c` bytes. Matching covers the
entire input. Without an argument, it uses `abcb`.

## Download

```sh
git clone https://github.com/Clydexy/fastregex.git
cd fastregex
```

If you already have a checkout, run the following commands from its root.

## Build with CMake

Install CMake 3.25 or newer, Ninja, and a C++23 compiler. See the root
[README](../README.md#prerequisites) for the currently validated toolchains.
Then configure, build, and run:

```sh
cmake -S . -B build/demo -G Ninja \
  -DFASTREGEX_BUILD_EXAMPLES=ON -DFASTREGEX_BUILD_TESTS=OFF
cmake --build build/demo --target fastregex_demo
./build/demo/examples/fastregex_demo abcb
```

Expected output:

```text
Pattern: a(b|c)*
Input: abcb
Result: MATCH
```

Examples are optional and disabled by default. The target links to
`fastregex::compile_time`, which supplies the include paths and C++23 requirement.

The C++ compiler constructs a DFA for `a(b|c)*` during compilation. When the
program runs, `full_match` reads the input bytes through that precompiled DFA
and reports whether the entire input matches. It does not parse a regex at
runtime. The `static_assert` in the demo also checks `abcb` during compilation.

## Try other inputs

| Command | Result |
| --- | --- |
| `./build/demo/examples/fastregex_demo` | `MATCH` (default input `abcb`) |
| `./build/demo/examples/fastregex_demo a` | `MATCH` |
| `./build/demo/examples/fastregex_demo abbbcc` | `MATCH` |
| `./build/demo/examples/fastregex_demo abx` | `NO MATCH` |
| `./build/demo/examples/fastregex_demo ''` | `NO MATCH` |

Exit status is 0 for a match, 1 for no match, and 2 for too many arguments.

To change the pattern, edit the template arguments in `demo.cpp`, update the
printed pattern and compile-time assertion, then rebuild. Patterns must be
compile-time literals; the command-line argument supplies only the input.
See the [language specification](../docs/byte-regex.md) for supported syntax.
