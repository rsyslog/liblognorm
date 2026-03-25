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

## Parser Change Guidance
1.  **Check the Whole Parser Family**: If you touch a parser implementation in `src/parser.c` or the legacy v1 parser files, search for the parser name in `tests/` and run all matching tests. This includes `*_jsoncnf.sh`, `*_v1.sh`, and terminator/edge-case variants when present.
2.  **Watch for v1/v2 Split**: Some parser names exist in both the modern PDAG parser code (`src/parser.c`) and the legacy v1 parser code (`src/v1_parser.c`, `src/v1_samp.c`, or `src/v1_parser.h`). When changing parser behavior, ensure you have identified and validated all relevant code paths if they exist.
3.  **Do Not Test During Relink**: Never run parser tests against `src/ln_test` while `make -C src ln_test` is still compiling or relinking. Wait for the build command to finish before executing any tests.
4.  **Prefer Family Validation Over Single Repros**: A local reproduction for one failing sample is not sufficient validation for parser work. Run the full parser-specific test family before committing or opening a PR. Prefer `ai/run-parser-family.sh <parser-name>` when it fits the change.

## Validation Ladder
1.  **Direct Repro**: Use a direct `ln_test` reproduction while isolating one specific parser failure or edge case.
2.  **Parser Family**: For parser behavior changes, run `ai/run-parser-family.sh <parser-name>` when a matching family exists.
3.  **Broader Coverage**: If the change touches shared parser plumbing, shell harness logic, or build/test infrastructure, run a broader targeted subset or `make check` before opening a PR.

## Commit Rules
1.  **Contextual Messages**: Commit messages must explain *why* a change was made, not just *what* changed. Relate changes to the project strategy (e.g., "AI first strategy") where applicable.
2.  **Attribution**: All AI-assisted commits must include the following footer:
    `With the help of AI Agent: <agent-name>`
3.  **Atomic Changes**: Separate documentation updates from code changes when possible, or group them logically if they are tightly coupled.
4.  **Relative Paths**: Always use relative paths in documentation (e.g., `doc/file.md`, not `file:///...`) to ensure portability across environments.
