#!/bin/bash
# added 2026-05-06 by AI agent
# This file is part of the liblognorm project, released under ASL 2.0
# shellcheck source=tests/exec.sh disable=SC1091,SC2154
. "$srcdir"/exec.sh

test_def "$0" "v2 parsers handle EOF safely"

add_rule 'version=2'
add_rule 'rule=:%f:string%'
execute 'abc'
assert_output_json_eq '{ "f": "abc" }'

reset_rules
add_rule 'version=2'
add_rule 'rule=:pre%f:json%'
execute 'pre'
assert_output_json_eq '{ "originalmsg": "pre", "unparsed-data": "" }'

reset_rules
add_rule 'version=2'
add_rule 'rule=:%f:json%'
execute '[1,2]'
assert_output_json_eq '{ "f": [ 1, 2 ] }'

reset_rules
add_rule 'version=2'
add_rule 'rule=:pre%f:op-quoted-string%'
execute 'pre'
assert_output_json_eq '{ "originalmsg": "pre", "unparsed-data": "" }'

reset_rules
add_rule 'version=2'
add_rule 'rule=:%f:checkpoint-lea%'
execute 'src:   '
assert_output_json_eq '{ "originalmsg": "src:   ", "unparsed-data": "src:   " }'

execute 'src: "unterminated'
assert_output_json_eq '{ "originalmsg": "src: \"unterminated", "unparsed-data": "src: \"unterminated" }'

execute 'src::'
assert_output_json_eq '{ "originalmsg": "src::", "unparsed-data": "src::" }'

cleanup_tmp_files
