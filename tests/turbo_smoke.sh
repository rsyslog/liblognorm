#!/bin/bash
# added 2026-04-22 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
srcdir="${srcdir:-.}"
# shellcheck disable=SC1091
. "$srcdir"/exec.sh

export ln_opts='-oturbo'

test_def "$0" "TurboVM end-to-end smoke path"
add_rule 'version=2'
add_rule 'rule=:%source.ip:ipv4% count=%count:number% msg=%msg:word%'

execute '192.0.2.5 count=42 msg=ready'
assert_output_json_eq '{ "source": { "ip": "192.0.2.5" }, "count": 42, "msg": "ready" }'


cleanup_tmp_files
