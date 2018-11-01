#!/bin/bash
# added 2014-11-17 by singh.janmejay
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh
no_solaris10
export ln_opts='-oallowRegex'

test_def $0 "regex field with negation"
add_rule 'rule=:%text:regex:[^,]+%,%more:rest%'
execute '123,abc'
assert_output_contains '"text": "123"'
assert_output_contains '"more": "abc"'
reset_rules
add_rule 'rule=:%text:regex:([^ ,|]+( |\||,)?)+%%more:rest%'
execute '123 abc|456 789,def|ghi,jkl| and some more text'
assert_output_contains '"text": "123 abc|456 789,def|ghi,jkl|"'
assert_output_contains '"more": " and some more text"'


cleanup_tmp_files

