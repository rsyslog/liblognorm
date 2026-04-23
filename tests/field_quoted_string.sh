#!/bin/bash
# added 2026-04-22 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
srcdir="${srcdir:-.}"
# shellcheck disable=SC1091
. "$srcdir"/exec.sh

test_def "$0" "quoted-string strips outer quotes"
add_rule 'version=2'
add_rule 'rule=:%f:quoted-string%'

execute '"alpha beta"'
assert_output_json_eq '{ "f": "alpha beta" }'

execute '""'
assert_output_json_eq '{ "f": "" }'

execute '"unterminated'
assert_output_json_eq '{ "originalmsg": "\"unterminated", "unparsed-data": "\"unterminated" }'


cleanup_tmp_files
