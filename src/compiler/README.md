# Compiler internals

Internal includes use `compiler/...` through the C++23 `fastregex_compiler`
target. They are not part of the consumer-facing API.

| Header | Purpose |
| --- | --- |
| `ast.hpp` | Fixed-capacity node and byte-set arenas, integer IDs, source spans |
| `parser.hpp` | `parse(pattern)`, shared by runtime and constant evaluation |
| `ast_dump.hpp` | `dump_ast(tree)`, deterministic text in arena order |

`parse` accepts an explicit-length `std::string_view` or a terminated character
array. The array overload excludes only the final terminator and preserves
embedded NUL. On failure, the result has a diagnostic and an empty tree.
Spans use half-open byte offsets; grouping widens its root's span.
