#!/bin/bash
# added 2022-03-28 by @KGuillemot
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh

test_def $0 "name/value parser"
add_rule 'version=2'
add_rule 'rule=:%{"name":"f", "type":"name-value-list", "separator":",", "assignator":":", "ignore_whitespaces":true}%'

execute 'name:value'
assert_output_json_eq '{ "f": { "name": "value" } }'

execute 'name1:value1,name2:value2,name3:value3'
assert_output_json_eq '{ "f": { "name1": "value1", "name2": "value2", "name3": "value3" } }'

execute ' name1: abcd, name2 : value2 ,name3 :value3 '
assert_output_json_eq '{ "f": { "name1": "abcd", "name2": "value2", "name3": "value3" } }'

# Check old behavior (default)
reset_rules
add_rule 'version=2'
add_rule 'rule=:%{"name":"f", "type":"name-value-list", "separator":",", "assignator":":"}%'

execute ' name1: abcd, name2 : value2 ,name3 :value3 '
assert_output_json_eq '{ "f": { " name1": " abcd", " name2 ": " value2 ", "name3 ": "value3 " } }'

cleanup_tmp_files

