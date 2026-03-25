# AI Workflow Helpers

This directory contains workflow helpers for developers and AI agents.
These files are not part of the liblognorm runtime or testbench itself.
They exist to make local validation more consistent and less error-prone.

## Current Helpers

### `run-parser-family.sh`

Run the full shell-test family for one parser name.

Examples:

```bash
ai/run-parser-family.sh checkpoint-lea
ai/run-parser-family.sh json
ai/run-parser-family.sh field_cef.sh
```

What it does:

1. normalizes the parser name to the `field_<parser>*` shell-test prefix
2. rebuilds `src/ln_test`
3. ensures `tests/json_eq` is available
4. runs every matching parser-family shell test in `tests/`

This is intended for parser work where a single direct repro is not
sufficient validation.

## Validation Ladder

Use the smallest validation step that is appropriate, but do not stop
too early for parser changes.

1. direct repro
   Useful while debugging a single failing sample or parser edge case.

2. parser-family helper
   Required for parser behavior changes when a matching family exists:
   `ai/run-parser-family.sh <parser-name>`

3. broader test run
   Use `make check` or a larger targeted subset when the change touches
   shared parser plumbing, shell harness logic, or build/test
   infrastructure.

## Notes

- Wait for `make -C src ln_test` to finish before running tests.
- Parser families often include `*_jsoncnf.sh`, `*_v1.sh`, and
  terminator or edge-case variants. The helper is meant to catch those
  automatically.
