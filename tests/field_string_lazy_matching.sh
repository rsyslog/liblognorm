#!/bin/bash
# added 2018-06-26 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh
no_solaris10

test_def $0 "string syntax"

reset_rules
add_rule 'version=2'
add_rule 'rule=:Rule-ID:%-:whitespace%%
        f:string{"matching.permitted":[
            {"class":"digit"},
            {"chars":"abcdefghijklmnopqrstuvwxyz"},
            {"chars":"ABCDEFGHIJKLMNOPQRSTUVWXYZ"},
            {"chars":"-"},
        ],
	"quoting.escape.mode":"none",
	"matching.mode":"lazy"}%%resta:rest%'

execute 'Rule-ID: XY7azl704-84a39894783423467a33f5b48bccd23c-a0n63i2\r\nQNas: '
assert_output_json_eq '{ "resta": "\\r\\nQNas: ", "f": "XY7azl704-84a39894783423467a33f5b48bccd23c-a0n63i2" }'

execute 'Rule-ID: XY7azl704-84a39894783423467a33f5b48bccd23c-a0n63i2 LWL'
assert_output_json_eq '{ "resta": " LWL", "f": "XY7azl704-84a39894783423467a33f5b48bccd23c-a0n63i2" }'

cleanup_tmp_files
