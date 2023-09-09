#!/bin/bash
# added 2021-06-07 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh
no_solaris10
test_def $0 "quoted string with quotesOptional"

add_rule 'version=2'
add_rule 'rule=:%
		{"type":"quoted-string", "name":"str", "option.quotesOptional":True}
	 %'

execute '"line 1"'
assert_output_json_eq '{ "str": "\"line 1\""}'

execute 'line2'
assert_output_json_eq '{ "str": "line2"}'


cleanup_tmp_files
