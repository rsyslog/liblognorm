#!/bin/bash
# added 2014-11-14 by singh.janmejay
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh
no_solaris10
export ln_opts='-oallowRegex'

test_def $0 "type ERE for regex field"
add_rule 'rule=:%first:regex:[a-z]+% %second:regex:\d+\x25\x3a[a-f0-9]+\x25%'
execute 'foo 122%:7a%'
assert_output_contains '"first": "foo"'
assert_output_contains '"second": "122%:7a%"'



cleanup_tmp_files

