#!/bin/bash
# TurboVM JSON field parity
# This file is part of the liblognorm project, released under ASL 2.0
#
# The TurboVM JSON field handler validates a JSON value and stores it verbatim,
# so arrays, booleans, nulls and full-precision numbers round-trip to the same
# JSON the v1 parser produces. Malformed JSON fails to match, exactly as v1.
srcdir="${srcdir:-.}"
# shellcheck disable=SC1091
. "$srcdir"/exec.sh

export ln_opts='-oturbo'

test_def "$0" "TurboVM JSON field parity"
add_rule 'version=2'
add_rule 'rule=:%field:json%'

# Object: keys and value types preserved (int 2, not "2").
execute '{"f1": "1", "f2": 2}'
assert_output_json_eq '{ "field": { "f1": "1", "f2": 2 } }'

# Nested object.
execute '{"outer": {"num": 3, "name": "value"}}'
assert_output_json_eq '{ "field": { "outer": { "num": 3, "name": "value" } } }'

# Booleans stay boolean, not quoted strings. Double compares with json_eq's
# tolerance so 1.5 == 1.50.
execute '{"i": 42, "d": 1.5, "b": true, "f": false, "s": "x"}'
assert_output_json_eq '{ "field": { "i": 42, "d": 1.5, "b": true, "f": false, "s": "x" } }'

# JSON null is preserved, not dropped.
execute '{"n": null, "x": 1}'
assert_output_json_eq '{ "field": { "n": null, "x": 1 } }'

# Arrays stay arrays, not index-keyed objects.
execute '["A", "B"]'
assert_output_json_eq '{ "field": ["A", "B"] }'

# Array nested in an object, and an array of objects.
execute '{"arr": [1, 2, 3], "nested": {"k": "v"}}'
assert_output_json_eq '{ "field": { "arr": [1, 2, 3], "nested": { "k": "v" } } }'
execute '[{"a": 1}, {"b": 2}]'
assert_output_json_eq '{ "field": [ { "a": 1 }, { "b": 2 } ] }'

# Trailing data: the field consumes only the JSON value, the rest matches on.
add_rule 'rule=:%field:json%end'
execute '{"f1": "1", "f2": 2}end'
assert_output_json_eq '{ "field": { "f1": "1", "f2": 2 } }'

# A non-JSON body does not match; only unparsed-data is emitted.
reset_rules
add_rule 'version=2'
add_rule 'rule=:%field:json%'
execute '{"f1": "1", f2: 2}'
assert_output_json_eq '{ "unparsed-data": "{\"f1\": \"1\", f2: 2}" }'

# Malformed numbers make the value fail to match, matching v1. Only forms that
# the v1 parser also rejects are used, so the end-to-end result is unparsed-data.
execute '{"n": 1+2-3}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": 1+2-3}" }'
execute '{"n": -}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": -}" }'
execute '{"n": .}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": .}" }'
execute '{"n": +5}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": +5}" }'
execute '{"n": .5}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": .5}" }'
execute '{"n": 1.2.3}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": 1.2.3}" }'
execute '{"n": --5}'
assert_output_json_eq '{ "unparsed-data": "{\"n\": --5}" }'

# Valid edge forms parse: negative int, fraction, signed exponent.
execute '{"a": -3, "b": 2.50, "c": 6.022e23}'
assert_output_json_eq '{ "field": { "a": -3, "b": 2.5, "c": 6.022e23 } }'

# Doubles keep full precision, with no fixed-decimal rounding.
execute '{"pi": 3.141592653589793}'
assert_output_json_eq '{ "field": { "pi": 3.141592653589793 } }'

# The decimal separator is always '.', regardless of the process LC_NUMERIC.
LC_ALL=de_DE.UTF-8 LC_NUMERIC=de_DE.UTF-8 execute '{"v": 1.5}'
assert_output_json_eq '{ "field": { "v": 1.5 } }'

# A malformed \u escape is not valid JSON, so the value fails to match (the 'z'
# makes \u non-hex). A well-formed \u escape parses (json_eq reads A as "A").
execute '{"s": "\u1z34"}'
assert_output_contains 'unparsed-data'
execute '{"s": "\u0041"}'
assert_output_json_eq '{ "field": { "s": "A" } }'

# Nesting is bounded to json-c's limit for v1 parity: 31 levels parse, 32 do not.
d31=$(python3 -c 'print("["*31 + "1" + "]"*31)')
execute "$d31"
assert_output_contains 'field'
d32=$(python3 -c 'print("["*32 + "1" + "]"*32)')
execute "$d32"
assert_output_contains 'unparsed-data'

cleanup_tmp_files
