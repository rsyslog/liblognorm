#!/bin/bash
# added 2014-11-17 by singh.janmejay
# This file is part of the liblognorm project, released under ASL 2.0
#test that tokenized disabled regex if parent context has it disabled
. $srcdir/exec.sh
no_solaris10

test_def $0 "tokenized field with regex based field"
add_rule 'rule=:%parts:tokenized:,:regex:[^, ]+% %text:rest%'
execute '123,abc,456,def foo bar'
assert_output_contains '"unparsed-data": "123,abc,456,def foo bar"'
assert_output_contains '"originalmsg": "123,abc,456,def foo bar"'

#and then enables it when parent context has it enabled
export ln_opts='-oallowRegex'
. $srcdir/exec.sh

test_def $0 "tokenized field with regex based field"
add_rule 'rule=:%parts:tokenized:,:regex:[^, ]+% %text:rest%'
execute '123,abc,456,def foo bar'
assert_output_contains '"parts": [ "123", "abc", "456", "def" ]'
assert_output_contains '"text": "foo bar"'


cleanup_tmp_files

