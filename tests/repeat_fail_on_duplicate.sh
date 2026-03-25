#!/bin/bash
# added 2026-03-25 by Codex
# This file is part of the liblognorm project, released under ASL 2.0

. $srcdir/exec.sh

test_def $0 "repeat failOnDuplicate rejects duplicate fields in one element"
add_rule 'version=2'
add_rule 'rule=:a %{"name":"numbers", "type":"repeat",
			"option.failOnDuplicate": true,
			"parser":[
			  {"name":"n", "type":"number"},
			  {"type":"literal", "text":":"},
			  {"name":"n", "type":"number"}
			  ],
			"while":
			  {"type":"literal", "text":", "}
       		   }% b'
execute 'a 1:2 b'
assert_output_json_eq '{ "originalmsg": "a 1:2 b", "unparsed-data": "1:2 b" }'

cleanup_tmp_files
