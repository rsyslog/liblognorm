#!/bin/bash
# added 2018-04-07 by Vincent Tondellier
# based on usrdef_twotypes.sh
# This file is part of the liblognorm project, released under ASL 2.0

. $srcdir/exec.sh

test_def $0 "nested user-defined types"
add_rule 'version=2'
add_rule 'type=@hex-byte:%..:hexnumber{"maxval": "255"}%'
add_rule 'type=@two-hex-bytes:%f1:@hex-byte% %f2:@hex-byte%'
add_rule 'type=@unused:stop'
add_rule 'rule=:two bytes %.:@two-hex-bytes% %-:@unused%'

execute 'two bytes 0xff 0x16 stop'
assert_output_json_eq '{ "f1": "0xff", "f2": "0x16" }'

cleanup_tmp_files
