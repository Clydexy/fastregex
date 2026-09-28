# Byte-regex specification

Examples show regex source; use C++ raw strings such as `R"(\x00)"`.
“Empty” means zero bytes.

## Matching

| Property | Rule |
| --- | --- |
| Result | Boolean full-string match; no search or captures |
| Alphabet | Unsigned bytes `00`–`FF`; case-sensitive, no Unicode or locale rules |
| Lengths | Explicit pattern and subject lengths; embedded NUL is data |
| C++ literals | Exclude only the final implicit terminator |
| Null pointers | Allowed only for length zero; no loads or pointer arithmetic |
| Execution | Precompiled DFA only; no backtracking, lazy construction or NFA fallback |

## Syntax

| Pattern | Meaning | Matches | Does not match |
| --- | --- | --- | --- |
| `a` | Literal | `a` | `A`, `ba` |
| `ab` | Concatenation | `ab` | `a`, `abc` |
| `a\|bc` | Alternation | `a`, `bc` | `abc` |
| `(ab)*` | Group repeated zero or more times | Empty, `ab`, `abab` | `aba` |
| `ab*` | Star applies to `b` | `a`, `ab`, `abb` | Empty |
| `[abc]` | One byte from a set | `a`, `b`, `c` | Empty, `ab` |
| `[a-c]` | Inclusive unsigned-byte range | `a`, `b`, `c` | `d` |
| `[^a]` | Any byte except `a` | NUL, newline, byte `FF` | `a`, Empty |
| `\x00\xFF` | Two explicit bytes | Bytes `00 FF` | Byte `00` |
| Empty, `()`, `()*`, `\|`, `\|\|` | Epsilon | Empty | Any nonempty subject |
| `a\|`, `\|a`, `a\|\|` | Empty branch is epsilon | Empty, `a` | `aa` |

Precedence: atoms → star → concatenation → alternation.
`ab|cd*` means `(ab)|(c(d*))`. Groups do not capture. Concatenation and alternation
associate left. `(a*)*` is valid; `a**` and a star without an operand are invalid.

```text
pattern       = alternation, end-of-input ;
alternation   = concatenation, { "|", concatenation } ;
concatenation = { repetition } ;
repetition    = atom, [ "*" ] ;
atom          = literal | hex-byte | class | "(", alternation, ")" ;
```

Outside classes, every byte is literal except `\ ( ) | * [ ] . ^ $ + ? { }`.
Whitespace, raw NUL and high bytes are literal. Reserved syntax is rejected:

| Not supported | Examples |
| --- | --- |
| Wildcards, anchors, extra quantifiers | `.`, `^a`, `a$`, `a+`, `a?`, `a{2}`, `a*?` |
| Special groups, lookaround, flags | `(?:a)`, `(?=a)`, `(?<name>a)`, `(?i)` |
| Shorthand escapes and backreferences | `\n`, `\d`, `\w`, `\1`, `\u0041` |

## Hex bytes and classes

**The only escape is `\xHH`: exactly two hex digits, case-insensitive.**
Use it for every special byte, including literal metacharacters. There are no
punctuation escapes or shorthand escapes. Decoded bytes never become operators.

| Source | Meaning |
| --- | --- |
| `\x00`, `\x0A`, `\x5C` | NUL, newline, backslash |
| `\x2A`, `\x7C`, `\x5D` | Literal star, pipe, closing bracket |
| `\x414` | `A` followed by `4` |
| `[a\x2Dz]` | One of `a`, `-`, `z` |
| `[\x00-\xFF]` | Any single byte |
| `[^\x00-\xFF]` | Matches nothing; its star matches only Empty |

Classes union their items; duplicates are harmless. A class item is one literal
or hex byte, optionally followed by `-` and another byte. Ranges are inclusive;
equal endpoints are valid. The raw bytes `\ [ ] ^ -` have special meaning:

| Rule | Example |
| --- | --- |
| `^` complements only immediately after `[` | `[^a]` valid; `[a^]` invalid |
| `]` always closes the class | `[]` and `[^]` invalid |
| `-` is only a range separator | `[-a]`, `[a-]`, `[z-a]`, `[a-b-c]` invalid |
| `[` cannot nest classes | `[a[b]]` and `[[:alpha:]]` invalid |
| Every other byte is literal; no set operators | `[.*+?]` is a set of punctuation; `[a&&b]` equals `[a&b]` |

## Errors

Syntax errors return `invalid_pattern` and the zero-based source-byte offset
where parsing first fails. At end-of-input, the offset is the pattern length.
A descending range points to the start of its right endpoint. Runtime and
constexpr parsing report the same error. Static compilation rejects invalid
patterns at C++ compile time; a failed compilation produces no matcher. The
diagnostic names the error and exposes its source-byte offset through
`compile_failure<code, offset>`.

| Invalid pattern | Offset |
| --- | ---: |
| `a)`, `a]`, `a+`, `(?=a)` | 1 |
| `*a` | 0 |
| `a**` | 2 |
| `(`, final backslash | 1 (end-of-input) |
| `[a-` | 3 (end-of-input) |
| `[]`, `[^]` | 1, 2 respectively |
| `[-a]`, `[a-]`, `[z-a]` | 1, 3, 3 respectively |
| `\n`, `\*` | 1 (expected `x`) |
| `\xG0`, `\x0` | 2, 3 respectively |

## Compilation limits

Limits are fixed and inclusive. Exceeding a limit or overflowing a counter
returns the corresponding error with offset 0, without truncation or fallback.

| Internal limit | Value | Counts | Error code |
| --- | ---: | --- | --- |
| `max_pattern_bytes` | 4,096 | Original source bytes | `pattern_bytes_exceeded` |
| `max_nesting` | 64 | Simultaneously open groups | `nesting_exceeded` |
| `max_ast_nodes` | 8,192 | Nodes created before simplification | `ast_nodes_exceeded` |
| `max_nfa_states` | 16,384 | All states created before pruning | `nfa_states_exceeded` |
| `max_nfa_edges` | 32,768 | All epsilon/byte-set edges created | `nfa_edges_exceeded` |
| `max_dfa_states` | 4,096 | Reachable nonempty subsets plus one rejecting sink | `dfa_states_exceeded` |
| `max_transitions` | 1,048,576 | 256 entries per DFA state, including sink | `transitions_exceeded` |
| `max_work` | 16,777,216 | Cumulative construction steps | `work_exceeded` |

AST counts include one node per byte-set, epsilon, star or binary join; groups
add none. Work counts source bytes consumed, nodes/states/edges processed,
worklist operations, comparisons, logical transitions and 64-bit set chunks
read/written, including repeated work. Totals depend on the compiler version.
Host compiler constexpr limits apply separately.
