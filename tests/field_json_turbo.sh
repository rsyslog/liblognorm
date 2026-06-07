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

cleanup_tmp_files
