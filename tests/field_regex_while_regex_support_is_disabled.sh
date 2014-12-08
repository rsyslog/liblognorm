# added 2014-11-14 by singh.janmejay
# This file is part of the liblognorm project, released under ASL 2.0
source ./exec.sh $0 "field regex, while regex support is disabled"
add_rule 'rule=:%first:regex:[a-z]+%'
execute 'foo'
assert_output_contains '"originalmsg": "foo"'
assert_output_contains '"unparsed-data": "foo"'
