#!/bin/bash
# TurboVM JSON deep-nesting depth parity
# This file is part of the liblognorm project, released under ASL 2.0
#
# Regression for the serializer depth ceiling. The VM JSON flattener accepted
# nesting up to LN_JSON_MAX_DEPTH, but the serializer re-nested only 8 levels
# (a separate #define plus an 8-bit comma-tracking bitmask), so dotted keys
# deeper than 8 were silently mis-nested or collapsed and sibling leaves that
# shared the first 8 levels overwrote each other. The flattener and serializer
# now share one LN_JSON_MAX_DEPTH and a 64-bit bitmask; deep objects must
# round-trip intact.
srcdir="${srcdir:-.}"
# shellcheck disable=SC1091
. "$srcdir"/exec.sh

export ln_opts='-oturbo'

test_def "$0" "TurboVM JSON deep nesting (beyond the old depth-8 ceiling)"
add_rule 'version=2'
add_rule 'rule=:%field:json%'

# 12 levels of nesting with short keys. The dotted key stays well under the
# 512-byte key scratch buffer, so this exercises the depth ceiling and not the
# key-length limit. Pre-fix the serializer collapsed everything past level 8.
execute '{"a":{"b":{"c":{"d":{"e":{"f":{"g":{"h":{"i":{"j":{"k":{"l":"deep"}}}}}}}}}}}}'
assert_output_json_eq '{ "field": {"a":{"b":{"c":{"d":{"e":{"f":{"g":{"h":{"i":{"j":{"k":{"l":"deep"}}}}}}}}}}}} }'

# Two leaves nested 9 deep that share the first 8 levels must both survive with
# distinct values (the exact pre-fix collision: past level 8 they overwrote one
# another). Leaves stay typed ints.
execute '{"a":{"b":{"c":{"d":{"e":{"f":{"g":{"h":{"x":1,"y":2}}}}}}}}}'
assert_output_json_eq '{ "field": {"a":{"b":{"c":{"d":{"e":{"f":{"g":{"h":{"x":1,"y":2}}}}}}}}} }'

cleanup_tmp_files
