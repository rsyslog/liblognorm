#!/bin/bash
# added 2014-12-15 by singh.janmejay
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh
no_solaris10

test_def $0 "descent based parsing field, with invalid ruledef"

#invalid parent field name
add_rule 'rule=:%net:desce%'
execute '10.20.30.40 foo'
assert_output_json_eq '{ "originalmsg": "10.20.30.40 foo", "unparsed-data": "10.20.30.40 foo" }'

#no args
add_rule 'rule=:%net:descent%'
execute '10.20.30.40 foo'
assert_output_json_eq '{ "originalmsg": "10.20.30.40 foo", "unparsed-data": "10.20.30.40 foo" }'

#incorrect rulebase file path
rm -f $srcdir/quux.rulebase
add_rule 'rule=:%net:descent:./quux.rulebase%'
execute '10.20.30.40 foo'
assert_output_json_eq '{ "originalmsg": "10.20.30.40 foo", "unparsed-data": "10.20.30.40 foo" }'

#invalid content in rulebase file
reset_rules
add_rule 'rule=:%net:descent:./child.rulebase%'
reset_rules 'child'
add_rule 'rule=:%ip_addr:ipv4 %tail:rest%' 'child'
execute '10.20.30.40 foo'
assert_output_json_eq '{ "originalmsg": "10.20.30.40 foo", "unparsed-data": "10.20.30.40 foo" }'

#empty child rulebase file
reset_rules
add_rule 'rule=:%net:descent:./child.rulebase%'
reset_rules 'child'
execute '10.20.30.40 foo'
assert_output_json_eq '{ "originalmsg": "10.20.30.40 foo", "unparsed-data": "10.20.30.40 foo" }'

#no rulebase given
reset_rules
add_rule 'rule=:%net:descent:'
reset_rules 'child'
execute '10.20.30.40 foo'
assert_output_json_eq '{ "originalmsg": "10.20.30.40 foo", "unparsed-data": "10.20.30.40 foo" }'

#no rulebase and no tail-field given
reset_rules
add_rule 'rule=:%net:descent::'
reset_rules 'child'
execute '10.20.30.40 foo'
assert_output_json_eq '{ "originalmsg": "10.20.30.40 foo", "unparsed-data": "10.20.30.40 foo" }'

#no rulebase given, but has valid tail-field
reset_rules
add_rule 'rule=:%net:descent::foo'
reset_rules 'child'
execute '10.20.30.40 foo'
assert_output_json_eq '{ "originalmsg": "10.20.30.40 foo", "unparsed-data": "10.20.30.40 foo" }'

#empty tail-field given
echo empty tail-field given
rm tmp.rulebase
reset_rules
add_rule 'rule=:A%net:descent:./child.rulebase:%'
reset_rules 'child'
add_rule 'rule=:%ip_addr:ipv4% %tail:rest%' 'child'
execute 'A10.20.30.40 foo'
assert_output_json_eq '{ "net": { "tail": "foo", "ip_addr": "10.20.30.40" } }'

#named tail-field not populated
echo tail-field not populated
reset_rules
add_rule 'rule=:%net:descent:./child.rulebase:foo% foo'
reset_rules 'child'
add_rule 'rule=:%ip_addr:ipv4% %tail:rest%' 'child'
execute '10.20.30.40 foo'
assert_output_json_eq '{ "originalmsg": "10.20.30.40 foo", "unparsed-data": "10.20.30.40 foo" }'


cleanup_tmp_files

