#!/bin/bash
# added 2026-09-08 by Codex
# This file is part of the liblognorm project, released under ASL 2.0

. $srcdir/exec.sh

test_def $0 "RFC3164 timestamp with a null format"
add_rule 'version=2'
add_rule 'rule=:%{"type":"date-rfc3164","name":"timestamp","format":null}%'

# A malformed format must not crash rulebase loading. Keep the default string
# representation, as is done for other unrecognized format values.
execute 'Aug 10 12:00:00'
assert_output_json_eq '{ "timestamp": "Aug 10 12:00:00" }'

cleanup_tmp_files
