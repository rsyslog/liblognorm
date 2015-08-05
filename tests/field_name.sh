# added 2015-08-04 by Kenneth Good
# This file is part of the liblognorm project, released under ASL 2.0

. $srcdir/exec.sh

test_def $0 "name field"
add_rule 'rule=:here is a name %name:name% value'
execute 'here is a name my.test1234-56_h value'
assert_output_json_eq '{"name": "my.test1234-56_h"}'

#check cases where parsing failure must occur
execute 'here is a name my.test1234-56_h$ value'
assert_output_json_eq '{ "originalmsg": "here is a name my.test1234-56_h$ value", "unparsed-data": "$ value" }'


cleanup_tmp_files

