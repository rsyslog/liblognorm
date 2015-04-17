# added 2015-03-12 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
source ./exec.sh $0 "kernel timestamp parser"
add_rule 'rule=:begin %timestamp:kernel-timestamp% end'
execute 'begin [12345.123456] end'
assert_output_json_eq '{ "timestamp": "[12345.123456]"}'

reset_rules

add_rule 'rule=:begin %timestamp:kernel-timestamp%'
execute 'begin [12345.123456]'
assert_output_json_eq '{ "timestamp": "[12345.123456]"}'

reset_rules

add_rule 'rule=:%timestamp:kernel-timestamp%'
execute '[12345.123456]'
assert_output_json_eq '{ "timestamp": "[12345.123456]"}'
