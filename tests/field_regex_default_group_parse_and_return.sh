# added 2014-11-14 by singh.janmejay
# This file is part of the liblognorm project, released under ASL 2.0
export ln_opts='-oallowRegex'
source ./exec.sh $0 "type ERE for regex field"
add_rule 'rule=:%first:regex:[a-z]+% %second:regex:b[a-z]{2}%'
execute 'foo bar'
assert_output_contains '"first": "foo"'
assert_output_contains '"second": "bar"'

