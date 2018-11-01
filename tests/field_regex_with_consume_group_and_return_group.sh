#!/bin/bash
# added 2014-11-14 by singh.janmejay
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh
export ln_opts='-oallowRegex'
no_solaris10

test_def $0 "regex field with consume-group and return-group"
set -x
add_rule 'rule=:%first:regex:[a-z]{2}(([a-f0-9]+),)+:0:2%%rest:rest%'
execute 'ad1234abcd,4567ef12,8901abef'
assert_output_contains '"first": "4567ef12"'
assert_output_contains '"rest": "8901abef"'
reset_rules
add_rule 'rule=:%first:regex:(([a-z]{2})(([a-f0-9]+),)+):2:4%%rest:rest%'
execute 'ad1234abcd,4567ef12,8901abef'
assert_output_contains '"first": "4567ef12"'
assert_output_contains '"rest": "1234abcd,4567ef12,8901abef"'



cleanup_tmp_files

