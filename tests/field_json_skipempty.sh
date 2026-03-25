# added 2018-08-27 by Noriko Hosoi
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh

test_def $0 "JSON field"
add_rule 'version=2'
add_rule 'rule=:%field:json%'

# default behaviour
execute '{"f1": "1", "f2": 2, "f3": "", "f4": {}, "f5": []}'
assert_output_json_eq '{ "field": { "f1": "1", "f2": 2 , "f3": "", "f4": {}, "f5": []} }'

# skip empty json values
reset_rules
add_rule 'version=2'
add_rule 'rule=:%field:json:skipempty%'

execute '{"f1": "1", "f2": 2, "f3": "", "f4": {}, "f5": []}'
assert_output_json_eq '{ "field": { "f1": "1", "f2": 2 } }'

# invalid parameter must be rejected at rule-load time
reset_rules
add_rule 'version=2'
add_rule 'rule=:%field:json:bogus%'

err_file="$test_tmpdir/test.err"

set +e
printf '%s\n' '{"f1": "1", "f2": 2, "f3": "", "f4": {}, "f5": []}' \
	| $cmd $ln_opts -r "$(rulebase_file_name)" -e json >"$test_out" 2>"$err_file"
rc=$?
set -e

if [ "$rc" -eq 0 ]; then
	echo "FAIL: invalid json skipempty config was accepted"
	exit 1
fi

grep -F "invalid flag for JSON parser: bogus" "$err_file"

cleanup_tmp_files
