# added 2015-08-04 by Kenneth Good
# This file is part of the liblognorm project, released under ASL 2.0

. $srcdir/exec.sh

test_def $0 "alnum field"
add_rule 'rule=:here is a alphanumeric %alnum:alnum% value'
execute 'here is a alphanumeric Test1234 value'
assert_output_json_eq '{"alnum": "Test1234"}'

#check cases where parsing failure must occur
execute 'here is a alphanumeric Test-1234 value'
assert_output_json_eq '{ "originalmsg": "here is a alphanumeric Test-1234 value", "unparsed-data": "Test-1234 value" }'


cleanup_tmp_files

