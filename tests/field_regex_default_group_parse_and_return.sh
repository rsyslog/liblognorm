# added 2014-11-14 by singh.janmejay
# This file is part of the liblognorm project, released under ASL 2.0
source ./exec.sh $0 "type ERE for regex field"
add_rule 'rule=:%first:regex:[a-z]+:ERE% %second:regex:b[a-z]{2}:ERE%'
execute 'foo bar'
assert_output_contains '"first": "foo"'
assert_output_contains '"second": "bar"'

