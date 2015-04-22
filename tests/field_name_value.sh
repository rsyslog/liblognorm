# added 2015-04-25 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
source ./exec.sh $0 "name/value parser"
add_rule 'rule=:%f:name-value-list%'
execute 'name=value'
assert_output_json_eq '{ "f": { "name": "value" } }'

execute 'name1=value1 name2=value2 name3=value3'
assert_output_json_eq '{ "f": { "name1": "value1", "name2": "value2", "name3": "value3" } }'

execute 'name1=value1 name2=value2 name3=value3 '
assert_output_json_eq '{ "f": { "name1": "value1", "name2": "value2", "name3": "value3" } }'

execute 'name1= name2=value2 name3=value3 '
assert_output_json_eq '{ "f": { "name1": "value1", "name2": "value2", "name3": "value3" } }'

execute 'name1 name2=value2 name3=value3 '
assert_output_json_eq '{ "f": { "name1": "value1", "name2": "value2", "name3": "value3" } }'
