#!/bin/bash
# added 2018-06-26 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh
no_solaris10

test_def $0 "string syntax"

reset_rules
add_rule 'version=2'
add_rule 'rule=:%f:string{"matching.permitted":[ {"class":"digit"} ], "matching.mode":"lazy"}
                   %%r:rest%'

execute '12:34 56'
assert_output_json_eq '{ "r": ":34 56", "f": "12" }'

cleanup_tmp_files
