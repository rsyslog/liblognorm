#!/bin/bash
# added 2021-05-15 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh
no_solaris10

test_def $0 "quoted string with escapes"
add_rule 'version=2'
add_rule 'rule=:%
		{"type":"quoted-string", "name":"str", "option.supportEscape":True}
	 %'

execute '"word1\"word2"'
assert_output_json_eq '{ "str": "\"word1\\\"word2\""}'


cleanup_tmp_files

