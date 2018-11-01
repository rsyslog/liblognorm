#!/bin/bash
# added 2015-09-02 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh
no_solaris10

test_def $0 "string type with permitted chars"

reset_rules
add_rule 'version=2'
add_rule 'rule=:a %f:string{"matching.permitted":"abc"}% b'

execute 'a abc b'
assert_output_json_eq '{"f": "abc"}'

execute 'a abcd b'
assert_output_json_eq '{"originalmsg": "a abcd b", "unparsed-data": "abcd b" }'

execute 'a abbbbbcccbaaaa b'
assert_output_json_eq '{"f": "abbbbbcccbaaaa"}'

execute 'a "abc" b'
assert_output_json_eq '{"f": "abc"}'

echo "param array"
reset_rules
add_rule 'version=2'
add_rule 'rule=:a %f:string{"matching.permitted":[
			       {"chars":"ab"},
			       {"chars":"c"}
                               ]}% b'

execute 'a abc b'
assert_output_json_eq '{"f": "abc"}'

reset_rules
add_rule 'version=2'
add_rule 'rule=:a %f:string{"matching.permitted":[
			       {"class":"digit"},
			       {"chars":"x"}
                               ]}% b'

execute 'a 12x3 b'
assert_output_json_eq '{"f": "12x3"}'


echo alpha
reset_rules
add_rule 'version=2'
add_rule 'rule=:a %f:string{"matching.permitted":[
			       {"class":"alpha"}
                               ]}% b'

execute 'a abcdefghijklmnopqrstuvwxyZ b'
assert_output_json_eq '{"f": "abcdefghijklmnopqrstuvwxyZ"}'

execute 'a abcd1 b'
assert_output_json_eq '{"originalmsg": "a abcd1 b", "unparsed-data": "abcd1 b" }'


echo alnum
reset_rules
add_rule 'version=2'
add_rule 'rule=:a %f:string{"matching.permitted":[
			       {"class":"alnum"}
                               ]}% b'

execute 'a abcdefghijklmnopqrstuvwxyZ b'
assert_output_json_eq '{"f": "abcdefghijklmnopqrstuvwxyZ"}'

execute 'a abcd1 b'
assert_output_json_eq '{"f": "abcd1" }'

execute 'a abcd1_ b'
assert_output_json_eq '{ "originalmsg": "a abcd1_ b", "unparsed-data": "abcd1_ b" } '

cleanup_tmp_files
