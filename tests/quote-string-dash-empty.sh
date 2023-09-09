#!/bin/bash
# added 2021-06-07 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh
no_solaris10
test_def $0 "quoted string with dash"

add_rule 'version=2'
add_rule 'rule=:%
		{"type":"quoted-string", "name":"str", "option.dashIsEmpty":True}
	 %'

execute '"-"'
assert_output_json_eq '{ "str": ""}'

reset_rules
add_rule 'version=2'
add_rule 'rule=:%
		{"type":"quoted-string", "name":"str"}
	 %'

execute '"-"'
assert_output_json_eq '{ "str": "\"-\""}'


cleanup_tmp_files

