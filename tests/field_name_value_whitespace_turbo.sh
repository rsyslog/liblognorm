#!/bin/bash
# TurboVM parity test for name-value-list "ignore_whitespaces".
# Mirrors field_name_value_whitespace.sh but runs the rulebase through the
# TurboVM bytecode engine (-oturbo); the expected output must be identical to
# the standard parser.
# This file is part of the liblognorm project, released under ASL 2.0
srcdir="${srcdir:-.}"
# shellcheck disable=SC1091
. "$srcdir"/exec.sh

export ln_opts='-oturbo'

test_def "$0" "name/value parser (TurboVM, ignore_whitespaces)"
add_rule 'version=2'
add_rule 'rule=:%{"name":"f", "type":"name-value-list", "separator":",", "assignator":":", "ignore_whitespaces":true}%'

execute 'name:value'
assert_output_json_eq '{ "f": { "name": "value" } }'

execute 'name1:value1,name2:value2,name3:value3'
assert_output_json_eq '{ "f": { "name1": "value1", "name2": "value2", "name3": "value3" } }'

execute ' name1: abcd, name2 : value2 ,name3 :value3 '
assert_output_json_eq '{ "f": { "name1": "abcd", "name2": "value2", "name3": "value3" } }'

execute 'name1:"value1" , name2 : "value2" , name3 : value3 '
assert_output_json_eq '{ "f": { "name1": "value1", "name2": "value2", "name3": "value3" } }'

execute 'name1:   , name2 : value2'
assert_output_json_eq '{ "f": { "name1": "", "name2": "value2" } }'

execute 'name1:   '
assert_output_json_eq '{ "f": { "name1": "" } }'

# Without ignore_whitespaces, TurboVM must preserve surrounding whitespace
# (regression guard: the option-off path must not trim). The input avoids a
# leading space on the message itself: trimming of the message's first
# character is a separate, pre-existing TurboVM/standard difference unrelated
# to this option.
reset_rules
add_rule 'version=2'
add_rule 'rule=:%{"name":"f", "type":"name-value-list", "separator":",", "assignator":":"}%'

execute 'name1 : abcd, name2 : value2 ,name3 :value3 '
assert_output_json_eq '{ "f": { "name1 ": " abcd", " name2 ": " value2 ", "name3 ": "value3 " } }'

cleanup_tmp_files
