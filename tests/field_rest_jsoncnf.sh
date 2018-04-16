#/bin/bash
# some more tests for the "rest" motif, especially to ensure that
# "rest" will not interfere with more specific rules.
# added 2015-04-27
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh

test_def $0 "rest matches"

#tail recursion with default tail field
add_rule 'version=2'
add_rule 'rule=:%{"name":"iface", "type":"char-to", "extradata":":"}%:%{"name":"ip", "type":"ipv4"}%/%{"name":"port", "type":"number"}% (%{"name":"label2", "type":"char-to", "extradata":")"}%)'
add_rule 'rule=:%{"name":"iface", "type":"char-to", "extradata":":"}%:%{"name":"ip", "type":"ipv4"}%/%{"name":"port", "type":"number"}% (%{"name":"label2", "type":"char-to", "extradata":")"}%)%{"name":"tail", "type":"rest"}%'
add_rule 'rule=:%{"name":"iface", "type":"char-to", "extradata":":"}%:%{"name":"ip", "type":"ipv4"}%/%{"name":"port", "type":"number"}%'
add_rule 'rule=:%{"name":"iface", "type":"char-to", "extradata":":"}%:%{"name":"ip", "type":"ipv4"}%/%{"name":"port", "type":"number"}%%{"name":"tail", "type":"rest"}%'

# real-world cisco samples
execute 'Outside:10.20.30.40/35 (40.30.20.10/35)'
assert_output_json_eq '{ "label2": "40.30.20.10\/35", "port": "35", "ip": "10.20.30.40", "iface": "Outside" }'

execute 'Outside:10.20.30.40/35 (40.30.20.10/35) with rest'
assert_output_json_eq '{  "tail": " with rest", "label2": "40.30.20.10\/35", "port": "35", "ip": "10.20.30.40", "iface": "Outside" }'

execute 'Outside:10.20.30.40/35 (40.30.20.10/35 brace missing'
assert_output_json_eq '{ "tail": " (40.30.20.10\/35 brace missing", "port": "35", "ip": "10.20.30.40", "iface": "Outside" }'

execute 'Outside:10.20.30.40/35 40.30.20.10/35'
assert_output_json_eq '{ "tail": " 40.30.20.10\/35", "port": "35", "ip": "10.20.30.40", "iface": "Outside" }'

#
# test expected mismatches
#
execute 'not at all!'
assert_output_json_eq '{ "originalmsg": "not at all!", "unparsed-data": "not at all!" }'

execute 'Outside 10.20.30.40/35 40.30.20.10/35'
assert_output_json_eq '{ "originalmsg": "Outside 10.20.30.40\/35 40.30.20.10\/35", "unparsed-data": "Outside 10.20.30.40\/35 40.30.20.10\/35" }'

execute 'Outside:10.20.30.40/aa 40.30.20.10/35'
assert_output_json_eq '{ "originalmsg": "Outside:10.20.30.40\/aa 40.30.20.10\/35", "unparsed-data": "aa 40.30.20.10\/35" }'


cleanup_tmp_files

