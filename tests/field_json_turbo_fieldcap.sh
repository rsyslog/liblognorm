#!/bin/bash
# TurboVM never truncates a result silently
# This file is part of the liblognorm project, released under ASL 2.0
#
# The fast result holds at most LN_FAST_MAX_FIELDS fields. When a message
# produces more, the VM raises LN_FRESULT_TRUNCATED and the string path
# (ln_turbo_normalize_to_str) REFUSES the result rather than serializing a
# document with fields missing, so ln_normalize_to_str() falls back to the
# recursive walker, which has no field limit.
#
# The vehicle has to be "%.:json%": that inlines every object key as its own
# field, so the count actually reaches the cap. A NAMED "%field:json%" stores
# the whole object as ONE raw-JSON field, so it never approaches the cap no
# matter how many keys it carries, and a test built on it passes even with the
# limit set to 1.
#
# The raw API (ln_turbo_normalize_raw) still returns the truncated result
# plus LN_FRESULT_TRUNCATED; it does not refuse. Callers decide. rsyslog's
# mmnormalize declines that result, falls back to ln_normalize, and counts
# it in the turbo.truncated impstats counter
# (rsyslog's tests/mmnormalize-turbo-truncated-stat.sh).
srcdir="${srcdir:-.}"
# shellcheck disable=SC1091
. "$srcdir"/exec.sh

export ln_opts='-oturbo'

test_def "$0" "a result past LN_FAST_MAX_FIELDS survives intact"
add_rule 'version=2'
add_rule 'rule=:%.:json%'

# 200 keys is past the cap on any sane setting of it. Every one must come back.
big=$(python3 -c 'print("{"+",".join("\"k%03d\":%d"%(i,i) for i in range(200))+"}")')
exp=$(python3 -c 'print("{"+",".join("\"k%03d\":%d"%(i,i) for i in range(200))+"}")')
execute "$big"
assert_output_json_eq "$exp"

cleanup_tmp_files
