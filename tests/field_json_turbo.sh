#!/bin/bash
# TurboVM OP_FIELD_JSON flatten parity
# This file is part of the liblognorm project, released under ASL 2.0
#
# Proves the TurboVM JSON field handler flattens into the flat store and
# round-trips through the serializer to the SAME nested JSON the v1 parser
# produces (no more whole-object stringification).
srcdir="${srcdir:-.}"
# shellcheck disable=SC1091
. "$srcdir"/exec.sh

export ln_opts='-oturbo'

test_def "$0" "TurboVM JSON field flatten + parity"
add_rule 'version=2'
add_rule 'rule=:%field:json%'

# Simple object: keys + types preserved (int 2, not "2").
execute '{"f1": "1", "f2": 2}'
assert_output_json_eq '{ "field": { "f1": "1", "f2": 2 } }'

# Nested object: dotted flat keys re-nest; int leaf stays int.
execute '{"outer": {"num": 3, "name": "value"}}'
assert_output_json_eq '{ "field": { "outer": { "num": 3, "name": "value" } } }'

# Mixed scalar types: int / double / bool / string.
# NOTE: bool is emitted as the STRING "true"/"false" by design (string-store
# convention); double compares with json_eq's 0.001 tolerance so 1.5 == 1.50.
execute '{"i": 42, "d": 1.5, "b": true, "s": "x"}'
assert_output_json_eq '{ "field": { "i": 42, "d": 1.5, "b": "true", "s": "x" } }'

# Trailing data tolerated (v1 parity): parser consumes only the JSON span.
add_rule 'rule=:%field:json%end'
execute '{"f1": "1", "f2": 2}end'
assert_output_json_eq '{ "field": { "f1": "1", "f2": 2 } }'

# Parse failure must NOT match (backtrack): the JSON field rejects malformed
# input just like v1 (turbo's no-match path emits only unparsed-data).
reset_rules
add_rule 'version=2'
add_rule 'rule=:%field:json%'
execute '{"f1": "1", f2: 2}'
assert_output_json_eq '{ "unparsed-data": "{\"f1\": \"1\", f2: 2}" }'

# Strict JSON-number validation: malformed numbers must FAIL the turbo parse
# (return -1 -> backtrack) rather than mis-consume invalid bytes as a single
# number. Pre-fix the turbo scanner silently consumed "1+2-3" as one number
# (strtod -> 1.0), MATCHING with a corrupt value and mis-parsing what followed;
# lone "-"/"." became 0.0. Turbo now rejects these. lognormalizer's v1 walker
# also rejects the forms tested below, so the end-to-end result is unparsed-data.
# (A few turbo-rejected forms like "1." / "1e" / "01" are accepted by the
# lenient v1 path and would still match here, so this test uses only forms
# rejected by both paths.)
reset_rules
add_rule 'version=2'
add_rule 'rule=:%field:json%'

# multiple sign/operator characters mid-number -> reject (was mis-consumed)
execute '{"n": 1+2-3}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": 1+2-3}" }'

# lone '-' is not a valid number -> reject (was 0.0)
execute '{"n": -}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": -}" }'

# lone '.' is not a valid number -> reject (was 0.0)
execute '{"n": .}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": .}" }'

# leading '+' is not valid JSON -> reject
execute '{"n": +5}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": +5}" }'

# bare '.' before fraction digits -> reject
execute '{"n": .5}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": .5}" }'

# two decimal points -> reject
execute '{"n": 1.2.3}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": 1.2.3}" }'

# double leading sign -> reject
execute '{"n": --5}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": --5}" }'

# Valid edge forms still parse correctly: negative int, fraction, signed exp.
execute '{"a": -3, "b": 2.50, "c": 6.022e23}'
assert_output_json_eq '{ "field": { "a": -3, "b": 2.5, "c": 6.022e23 } }'

# Long number (span > the 64-byte stack buffer): must reach strtod IN FULL via
# the arena path. The old code copied only the first 63 bytes before strtod, so
# a long mantissa could be cut short of its trailing exponent and the magnitude
# silently corrupted. This 70-digit integer overflows int64 -> kept as a double;
# the value must round-trip to ~1e69, not the ~1e62 a 63-char truncation gives.
big=$(python3 -c 'print("1" + "0"*69)')
execute "{\"big\": $big}"
assert_output_json_eq '{ "field": { "big": 1e69 } }'

# Locale-independence: the decimal separator is always '.' regardless of the
# process LC_NUMERIC. Under a comma-decimal locale, plain strtod() would parse
# "1.5" as 1.0; turbo parses via a private C locale (strtod_l) so it stays 1.5.
# (Falls back to the C locale itself if de_DE is not installed -> still 1.5.)
LC_ALL=de_DE.UTF-8 LC_NUMERIC=de_DE.UTF-8 execute '{"v": 1.5}'
assert_output_json_eq '{ "field": { "v": 1.5 } }'

# Malformed \u escape must be emitted literally INCLUDING its backslash (the old
# code dropped the '\' and emitted only "u1z34"). The 'z' makes \u non-hex.
execute '{"s": "\u1z34"}'
assert_output_json_eq '{ "field": { "s": "\\u1z34" } }'

# Key-buffer overflow must FAIL the parse (backtrack), not silently mislabel the
# leaf under a truncated key. A deeply-nested path whose dotted key exceeds the
# 512-byte scratch buffer must not match.
deep=$(python3 -c 'print("".join("{\"aaaaaaaaaaaaaaaa%d\":"%i for i in range(40)) + "1" + "}"*40)')
execute "$deep"
assert_output_contains 'unparsed-data'

cleanup_tmp_files
