#!/bin/bash
# added 2021-11-08 by @KGuillemot
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh

test_def $0 "name/value parser"
add_rule 'version=2'
add_rule 'rule=:%f:name-value-list%'

execute 'name="value"'
assert_output_json_eq '{ "f": { "name": "value" } }'

execute 'name1="value1" name2="value2" name3="value3"'
assert_output_json_eq '{ "f": { "name1": "value1", "name2": "value2", "name3": "value3" } }'

execute 'name1="value1 name2=value2" name3=value3 '
assert_output_json_eq '{ "f": { "name1": "value1 name2=value2", "name3": "value3" } }'

execute 'name1="" name2="value2" name3="value3" '
assert_output_json_eq '{ "f": { "name1": "", "name2": "value2", "name3": "value3" } }'

execute 'origin="core.action" processed=67 failed=0 suspended=0 suspended.duration=0 resumed=0 '
assert_output_json_eq '{ "f": { "origin": "core.action", "processed": "67", "failed": "0", "suspended": "0", "suspended.duration": "0", "resumed": "0" } }'

# check escaped caracters
execute 'name1="a\"b" name2="c\\\"d" name3="e\\\\\"f" '
assert_output_json_eq '{ "f": { "name1": "a\"b", "name2": "c\\\"d", "name3": "e\\\\\"f" } }'

execute 'name1="a\"b\\" name2="c\\\"d\\\\" name3="e\\\\\"f\\\\\\" '
assert_output_json_eq '{ "f": { "name1": "a\"b\\", "name2": "c\\\"d\\\\", "name3": "e\\\\\"f\\\\\\" } }'

# check for required non-matches
execute 'name'
assert_output_json_eq ' {"originalmsg": "name", "unparsed-data": "name" }'

# check escaped caracters
execute 'name1="" rest'
assert_output_json_eq ' {"originalmsg": "name1=\"\" rest", "unparsed-data": "rest" }'

execute 'noname1 name2="value2" name3="value3" '
assert_output_json_eq '{ "originalmsg": "noname1 name2=\"value2\" name3=\"value3\" ", "unparsed-data": "noname1 name2=\"value2\" name3=\"value3\" " }'


cleanup_tmp_files

