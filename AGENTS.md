# AI Agent Context for liblognorm

## Project Overview
**liblognorm** is a fast, samples-based log normalization library. It parses log messages (strings) into structured JSON objects based on a set of rules.

## Critical Context
> [!IMPORTANT]
> **READ THIS FIRST**: The core engine logic is documented in `doc/pdag_implementation_model.rst`. This file contains critical "implementation deltas" that explain how the actual C code differs from the theoretical design (Master's thesis). **Ignoring this file will lead to incorrect assumptions about the parser's behavior.**

## Key Directories
- `src/`: Source code.
- `doc/`: Documentation (Sphinx reStructuredText).
- `tests/`: Test suite.

## Architecture Map
For a detailed breakdown of which files implement which concepts, see:
[doc/ai_architecture_map.md](doc/ai_architecture_map.md)

## Coding Standards & Constraints
1.  **C99**: The codebase is written in C99.
2.  **Memory Management**: Be careful with memory. The PDAG engine relies on specific ownership rules (see `pdag_implementation_model.rst`).
3.  **Thread Safety**: The library is designed to be thread-safe.
4.  **Error Handling**: Uses `goto done` pattern for cleanup and error propagation.

## Common Tasks
- **Adding a new parser**: See `src/parser.c`. Use the `PARSER_Parse`, `PARSER_Construct`, etc. macros.
- **Debugging**: The library has a debug callback system (`ln_setDebugCB`).

## Commit Rules
1.  **Contextual Messages**: Commit messages must explain *why* a change was made, not just *what* changed. Relate changes to the project strategy (e.g., "AI first strategy") where applicable.
2.  **Attribution**: All AI-assisted commits must include the following footer:
    `With the help of AI Agent: <agent-name>`
3.  **Atomic Changes**: Separate documentation updates from code changes when possible, or group them logically if they are tightly coupled.
4.  **Relative Paths**: Always use relative paths in documentation (e.g., `doc/file.md`, not `file:///...`) to ensure portability across environments.
