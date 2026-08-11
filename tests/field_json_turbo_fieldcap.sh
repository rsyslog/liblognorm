#!/bin/bash
# TurboVM JSON flatten field-count cap
# This file is part of the liblognorm project, released under ASL 2.0
#
# The JSON flattener stops at LN_FAST_MAX_FIELDS (64) and marks the result
# truncated (turbo_vm.c json_emit_* -> LN_FRESULT_TRUNCATED). This checks the
# observable half: a 65-key object flattens to exactly the first 64 keys, the
# 65th (last in input order) being dropped. The truncation FLAG that this same
# path raises is asserted end to end by rsyslog's mmnormalize-turbo-truncated
# -stat testbench test (mmnormalize/turbo.truncated impstats counter).
srcdir="${srcdir:-.}"
# shellcheck disable=SC1091
. "$srcdir"/exec.sh

export ln_opts='-oturbo'

test_def "$0" "TurboVM JSON flatten caps at LN_FAST_MAX_FIELDS (64)"
add_rule 'version=2'
add_rule 'rule=:%field:json%'

# 65 keys k00..k64 (zero-padded so the retained set is unambiguous). The
# flattener adds in input order and stops after 64, so k00..k63 survive and k64
# is dropped. json_eq compares structurally, so key order in the expected value
# does not matter.
big=$(python3 -c 'print("{"+",".join("\"k%02d\":%d"%(i,i) for i in range(65))+"}")')
exp=$(python3 -c 'print("{ \"field\": {"+",".join("\"k%02d\":%d"%(i,i) for i in range(64))+"} }")')
execute "$big"
assert_output_json_eq "$exp"

cleanup_tmp_files
