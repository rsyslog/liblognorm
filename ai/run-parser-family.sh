#!/bin/bash
# added 2026-03-25 by Codex
# This file is part of the liblognorm project, released under ASL 2.0
#
# Helper for developers and AI agents validating parser changes.
#
# This is intentionally kept outside of ./tests because it is not part of the
# project testbench itself. It is workflow tooling that enforces the local
# validation policy documented in AGENTS.md:
# - rebuild src/ln_test before running parser tests
# - build tests/json_eq when JSON comparison helpers are needed
# - run the whole parser-family test set for a parser name, including
#   *_jsoncnf.sh, *_v1.sh, and edge-case variants when present
#
# Usage:
#   ai/run-parser-family.sh checkpoint-lea
#   ai/run-parser-family.sh field_checkpoint-lea.sh
#
# The parser name is normalized to the shell test prefix field_<parser>* and
# every matching shell test in ./tests is executed sequentially.

set -euo pipefail

usage() {
	printf 'Usage: %s <parser-name>\n' "$0" >&2
	printf 'Example: %s checkpoint-lea\n' "$0" >&2
	exit 1
}

[ $# -eq 1 ] || usage

parser_name="$1"
parser_name="${parser_name#field_}"
parser_name="${parser_name%.sh}"

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
tests_dir="${repo_root}/tests"

mapfile -t tests < <(
	cd "${tests_dir}"
	printf '%s\n' field_"${parser_name}"*.sh | sort
)

if [ "${#tests[@]}" -eq 0 ] || [ "${tests[0]}" = "field_${parser_name}*.sh" ]; then
	printf 'No parser-family tests found for %s\n' "${parser_name}" >&2
	exit 1
fi

make -C "${repo_root}/src" ln_test
make -C "${tests_dir}" json_eq

for test_name in "${tests[@]}"; do
	(
		cd "${tests_dir}"
		srcdir=. top_builddir=.. bash "./${test_name}"
	)
done
