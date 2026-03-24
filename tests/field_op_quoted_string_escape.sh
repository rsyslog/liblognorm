#!/bin/bash
# added 2026-03-24 by AI agent
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh
no_solaris10

test_def $0 "op-quoted-string escape handling"

add_rule 'version=2'
add_rule 'rule=:%{"name":"f", "type":"op-quoted-string", "escape":true}%'

execute '"test with \" quote"'
assert_output_json_eq '{ "f": "test with \" quote" }'

execute '"test with \\ slash"'
assert_output_json_eq '{ "f": "test with \\ slash" }'

execute '"mixed \\ and \" escapes"'
assert_output_json_eq '{ "f": "mixed \\ and \" escapes" }'

execute 'word'
assert_output_json_eq '{ "f": "word" }'

cleanup_tmp_files
