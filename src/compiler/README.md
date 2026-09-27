# Compiler internals

Place the parser, automata construction, optimisation and backend implementation
here as they are implemented. Internal includes use
`compiler/...` through the `fastregex_compiler` target, which requires C++23.
Do not add this directory to the consumer-facing target's include paths.
