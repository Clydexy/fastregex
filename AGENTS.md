# Working on Fastregex

## Scope and architecture

- Build a byte-oriented, full-string DFA regex engine. Matching uses explicit
  lengths and unsigned bytes, including embedded NUL. Keep the NFA as a compiler
  intermediate; do not introduce backtracking or an implicit NFA fallback.
- Compiler internals live in `src/compiler/` and use C++23. Keep the consumer
  interface in `include/fastregex/` small and respect its separately declared
  language requirement. Adopt C++26 features only with a concrete benefit and
  an explicitly supported toolchain.
- Share constexpr-capable algorithms between runtime testing and enforced
  constant evaluation. Bound construction work and storage; report syntax and
  budget failures rather than truncating output or changing semantics.
- Keep parsing, NFA construction, DFA optimisation, native lowering, instruction
  encoding and object packaging separate. Initial native target: macOS arm64.
- Make output deterministic. Check equivalence when changing automata and
  measure runtime, code size and compilation cost when claiming an optimisation.
- The shared plan and design are in the
  [Linear project](https://linear.app/compile-time-regex/project/fastregex-compile-time-dfa-engine-6b3db65da5ba).
  Work within the requested issue's scope. Local research/planning files are
  ignored deliberately; do not force-add them. Keep permanent API/build docs,
  tests and this file tracked.

## Code conventions

- Use `.clang-format` for layout and `.clang-tidy` for static analysis. Prefer
  clear names, small functions, RAII and value types; follow existing conventions
  rather than imposing a naming scheme or arbitrary function-length limits.
- Use fixed-width unsigned types for bytes and instruction encodings. Check
  narrowing conversions, shifts, offsets and lengths before low-level access.
  Pointer arithmetic, bit masks, explicit casts and bounded arrays are valid
  when they make the compiler/backend clearer and their invariants are tested.
- Check length before every input load. Generated machine code is not covered
  by C++ sanitizer instrumentation; validate it with ABI and guard-page tests.
- Keep test/tool dependencies optional for consumers. Do not set global CMake
  flags in a way that changes a parent project or add runtime generator work.
- Avoid unrelated refactors and broad automatic lint fixes. For a justified
  false positive, use `NOLINTNEXTLINE(check-name)` with a specific explanation;
  do not disable the check globally just to pass a change.

## Required verification before every commit and every push

Run clang-tidy before **each commit and each push**, including documentation-only
changes. Re-run after any subsequent code/configuration edits. Analyse all
first-party translation units in the compilation database, not only changed
lines, so header changes are covered. A successful build alone is not a lint run.

From the repository root:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
run-clang-tidy -p build/debug '(^|/)(src|tests|tools|benchmarks|examples)/'

cmake --preset release
cmake --build --preset release
ctest --preset release
```

The presets export `build/debug/compile_commands.json`. Use the matching
clang-tidy and run-clang-tidy tools from an LLVM installation that supports our
C++ mode and SDK. On macOS, Homebrew LLVM provides them; Apple Clang alone may
not. If the runner is named `run-clang-tidy.py`, use that equivalent command.
Enable optional targets in a separate development build when changing their
code, and lint that compilation database too. Make sure changed private headers
are included by an analysed translation unit. Do not lint vendored/generated
sources or use `-fix` across the repository without reviewing the changes.

Check formatting of edited C++ files with
`clang-format --dry-run --Werror <files>` and check whitespace with
`git diff --check` (also `git diff --cached --check` before committing).
Resolve all enabled lint diagnostics, rerun affected checks, and inspect the diff.
If tools are missing or a check cannot run, report the blocker and do not commit
or push while claiming verification passed. These are contributor instructions,
not installed Git hooks; do not bypass checks with `--no-verify`.

Run additional targeted tests when the change warrants them: compile-fail tests
for diagnostics, differential/equivalence tests for compiler changes, sanitizer
and fuzz regressions for memory handling, and ABI/encoding/boundary tests for
the native backend. Record the commands and results in the completion summary.
Do not commit or push unless the user has requested it.

## Commit conventions

- Use Conventional Commits: `<type>(<optional scope>): <description>` (for
  example, `build(cmake): initialise project targets and test harness`).
- Cryptographically sign every commit using the configured SSH or GPG key
  (`git commit -S`). A `Signed-off-by` trailer is not a cryptographic signature.
- Verify the signature before pushing. If signing is unavailable or fails,
  resolve the signing setup before committing; do not create an unsigned commit
  or disable signing to bypass the failure.
