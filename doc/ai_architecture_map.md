# liblognorm Architecture Map

This document maps high-level concepts to specific files and functions in the codebase. Use this to navigate the source code efficiently.

## Core Components

| Concept | Description | Key Files |
| :--- | :--- | :--- |
| **Context** | The main library context, holding global state and configuration. | `src/liblognorm.c` (`ln_initCtx`), `src/liblognorm.h` (`struct ln_ctx_s`) |
| **PDAG** | **Parse DAG**. The core data structure for the rulebase. A directed acyclic graph where edges are "motifs" (parsers). | `src/pdag.c`, `src/pdag.h` |
| **Parser / Motif** | A specific matcher (e.g., "word", "number", "literal"). Edges in the PDAG. | `src/parser.c` (implementations), `src/parser.h` (interface) |
| **Sample** | A loaded rule or sample line. | `src/samp.c`, `src/samp.h` |
| **Annotation** | Metadata attached to parsed fields. | `src/annot.c`, `src/annot.h` |

## Data Flow

1.  **Initialization**: User calls `ln_initCtx()`.
2.  **Loading**: User calls `ln_loadSamples()`.
    *   `src/samp.c` parses the rule file.
    *   `src/pdag.c` builds the PDAG from the samples.
3.  **Normalization**: User calls `ln_normalize()`.
    *   `src/lognorm.c` is the entry point.
    *   It delegates to `ln_pdagParse()` in `src/pdag.c`.
    *   `ln_pdagParse()` traverses the graph, calling `ln_v2_parse*` functions in `src/parser.c`.

## Critical Data Structures

### `ln_ctx` (`src/liblognorm.h`)
The "world" object. Contains:
- `pdag`: The root of the rulebase.
- `debugCB`: Callback for debug logging.

### `ln_pdag` (`src/pdag.h`)
Represents the rulebase graph.

### `ln_parser` (`src/parser.h`)
Represents a single node/edge type in the graph.

## Testing
- **Tests Directory**: `tests/`
- **Running Tests**: `make check`
- **New Tests**: Add a new `.sh` file in `tests/` and add it to `TESTS` in `tests/Makefile.am`.

## Documentation
- **User Guide**: `doc/configuration.rst` (Rule syntax)
- **Internals**: `doc/internals.rst` (General concepts), `doc/pdag_implementation_model.rst` (Deep dive into the engine)
