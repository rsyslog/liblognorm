#!/bin/bash
# added 2015-04-13 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh

test_def $0 "cisco-interface-spec-at-EOL syntax"
add_rule 'version=2'
add_rule 'rule=:begin %field:cisco-interface-spec%%r:rest%'

execute 'begin outside:192.0.2.1/50349 end'
assert_output_json_eq '{ "r": " end", "field": { "interface": "outside", "ip": "192.0.2.1", "port": "50349" } }'

execute 'begin outside:192.0.2.1/50349'
assert_output_json_eq '{ "r": "", "field": { "interface": "outside", "ip": "192.0.2.1", "port": "50349" } }'

cleanup_tmp_files
