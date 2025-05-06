#!/bin/bash
# added 2025-05-06 by KGuillemot
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh

test_def $0 "float field"
add_rule 'version=2'
add_rule 'rule=:%test:op-quoted-string%%rest:rest%'
execute 'abcd efgh'
assert_output_json_eq '{"test": "abcd", "rest": " efgh"}'

reset_rules

add_rule 'version=2'
add_rule 'rule=:%test:op-quoted-string%'
execute '"abcd efgh"'
assert_output_json_eq '{"test": "abcd efgh"}'

execute '"abcd\\\"efgh"'
assert_output_json_eq '{"test": "abcd\\\"efgh"}'

reset_rules

add_rule 'version=2'
add_rule 'rule=:%test:op-quoted-string%%rest:rest%'

execute '"abcd\"efgh"ijkl'
assert_output_json_eq '{"test": "abcd\"efgh", "rest": "ijkl"}'

execute '"abcd\\"efgh"'
assert_output_json_eq '{"test": "abcd\\", "rest": "efgh\""}'

execute '"abcd\\\"efgh"'
assert_output_json_eq '{"test": "abcd\\\"efgh", "rest": ""}'

# Unclosed string (last quote is escaped)
execute '"abcd efgh\"'
assert_output_json_eq '{ "originalmsg": "\"abcd efgh\\\"", "unparsed-data": "\"abcd efgh\\\"" }'

reset_rules

add_rule 'version=2'
add_rule 'rule=:%test:op-quoted-string% %test2:op-quoted-string%'

execute '"abcd\"efgh\\" "ijkl\\\"mnop"'
assert_output_json_eq '{ "test2": "ijkl\\\"mnop", "test": "abcd\"efgh\\" }'

execute '"abcd\"efgh" "ijkl'
assert_output_json_eq '{ "originalmsg": "\"abcd\\\"efgh\" \"ijkl", "unparsed-data": "\"ijkl" }'

cleanup_tmp_files

