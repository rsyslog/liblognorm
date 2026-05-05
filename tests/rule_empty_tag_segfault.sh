#!/bin/bash
# added 2026-05-05 by AI agent
# This file is part of the liblognorm project, released under ASL 2.0

# shellcheck source=tests/exec.sh disable=SC2154
. "$srcdir"/exec.sh

test_def "$0" "empty v1 tag list entry is handled without segfault"

add_rule 'rule=,'

execute 'x'
assert_output_json_eq '{ "originalmsg": "x", "unparsed-data": "x" }'

cleanup_tmp_files
