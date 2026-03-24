#!/bin/bash
# added 2026-03-24 by AI agent
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh

test_def $0 "op-quoted-string invalid escape config"

add_rule 'version=2'
add_rule 'rule=:%{"name":"f", "type":"op-quoted-string", "escape":"yes"}%'

err_file="$test_tmpdir/test.err"

set +e
printf '"value"\n' | $cmd $ln_opts -r "$(rulebase_file_name)" -e json >"$test_out" 2>"$err_file"
rc=$?
set -e

if [ "$rc" -eq 0 ]; then
	echo "FAIL: invalid op-quoted-string escape config was accepted"
	exit 1
fi

grep -F "op-quoted-string's 'escape' field should be boolean" "$err_file"

cleanup_tmp_files
