#!/bin/bash
# TurboVM long field-name handling via the program string pool
# This file is part of the liblognorm project, released under ASL 2.0
#
# Field and context names longer than the inline opcode buffers (str[60] for
# most fields, char_to.name[56] for the char-to family) are interned in the
# program string pool and referenced by offset, so turbo parses them natively
# with no length limit and no v1 fallback, matching the v1 parser. Before this,
# an over-long name silently corrupted or truncated the field on the turbo path.
srcdir="${srcdir:-.}"
# shellcheck disable=SC1091
. "$srcdir"/exec.sh

export ln_opts='-oturbo'

test_def "$0" "TurboVM long field names via the string pool"
add_rule 'version=2'

# 65-char dotted ECS name, word parser (> the 60-byte inline buffer).
add_rule 'rule=:%threat.enrichments.indicator.file.x509.subject.distinguished_name:word%'
execute 'Hi'
assert_output_json_eq '{ "threat": { "enrichments": { "indicator": { "file": { "x509": {
	"subject": { "distinguished_name": "Hi" } } } } } } }'

# char-to family uses the smaller char_to.name[56] buffer; a 61-char name must
# also pool. Terminator ":" then a literal rest.
reset_rules
add_rule 'version=2'
add_rule 'rule=:%a.very.long.charto.field.name.that.exceeds.fifty.six.chars.xx:char-to{"extradata":":"}%:rest'
execute 'Hi:rest'
assert_output_json_eq '{ "a": { "very": { "long": { "charto": { "field": { "name": {
	"that": { "exceeds": { "fifty": { "six": { "chars": { "xx": "Hi" } } } } } } } } } } } }'

cleanup_tmp_files
