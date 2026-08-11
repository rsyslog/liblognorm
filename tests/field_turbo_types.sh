#!/bin/bash
# TurboVM field-type parity with the standard parser
# This file is part of the liblognorm project, released under ASL 2.0
#
# Guards the field types whose TurboVM output must match the standard parser:
# number and hexnumber keep the matched text as a string, whitespace stores the
# matched run and requires at least one character, and CEF extensions nest under
# the field rather than at the root.
srcdir="${srcdir:-.}"
# shellcheck disable=SC1091
. "$srcdir"/exec.sh

export ln_opts='-oturbo'

test_def "$0" "TurboVM field-type parity"

# number: the matched digits are stored as a string, not a JSON integer.
add_rule 'version=2'
add_rule 'rule=:%n:number% z'
execute '12345 z'
assert_output_json_eq '{ "n": "12345" }'

# hexnumber: the matched hex literal is stored verbatim, not its decimal value.
reset_rules
add_rule 'version=2'
add_rule 'rule=:%h:hexnumber% z'
execute '0xff z'
assert_output_json_eq '{ "h": "0xff" }'

# format="number" is honored: number/hexnumber/float emit native JSON numbers.
reset_rules
add_rule 'version=2'
add_rule 'rule=:%{"name":"n","type":"number","format":"number"}% z'
execute '12345 z'
assert_output_json_eq '{ "n": 12345 }'
reset_rules
add_rule 'version=2'
add_rule 'rule=:%{"name":"h","type":"hexnumber","format":"number"}% z'
execute '0xff z'
assert_output_json_eq '{ "h": 255 }'
reset_rules
add_rule 'version=2'
add_rule 'rule=:%{"name":"f","type":"float","format":"number"}% z'
execute '3.14159 z'
assert_output_json_eq '{ "f": 3.14159 }'

# whitespace: the matched run of spaces/tabs is stored, and at least one
# whitespace character is required (no match otherwise).
reset_rules
add_rule 'version=2'
add_rule 'rule=:a%w:whitespace%b'
execute 'a   b'
assert_output_json_eq '{ "w": "   " }'
execute 'ab'
assert_output_contains 'unparsed-data'

# CEF: the header fields and the key=value extensions all nest under the field
# name, with the extensions under an "Extensions" sub-object.
reset_rules
add_rule 'version=2'
add_rule 'rule=:%evt:cef%'
execute 'CEF:0|V|P|1.0|100|n|5|src=1.2.3.4 dst=5.6.7.8'
assert_output_json_eq '{ "evt": { "DeviceVendor": "V", "DeviceProduct": "P", "DeviceVersion": "1.0", "SignatureID": "100", "Name": "n", "Severity": "5", "Extensions": { "src": "1.2.3.4", "dst": "5.6.7.8" } } }'

cleanup_tmp_files
