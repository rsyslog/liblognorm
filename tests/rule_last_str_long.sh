#!/bin/bash
. $srcdir/exec.sh
no_solaris10

test_def $0 "multiple formats including string (see also: rule_last_str_short.sh)"
add_rule 'version=2'
add_rule 'rule=:%string:string%'
add_rule 'rule=:before %string:string%'
add_rule 'rule=:%string:string% after'
add_rule 'rule=:before %string:string% after'
add_rule 'rule=:before %string:string% middle %string:string%'

execute 'string'
execute 'before string'
execute 'string after'
execute 'before string after'
execute 'before string middle string'
assert_output_json_eq '{"string": "string" }' '{"string": "string" }''{"string": "string" }''{"string": "string" }''{"string": "string", "string": "string" }'


cleanup_tmp_files
