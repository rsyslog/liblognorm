#!/bin/bash
# added 2023-02-14 by Kevin Guillemot
# This file is part of the liblognorm project, released under ASL 2.0

. $srcdir/exec.sh

test_def $0 "Repeat with one parser named dot"
add_rule 'version=2'
add_rule 'rule=:a %{"name":"numbers", "type":"repeat",
			"parser":[
			  {"type":"number"},
			  {"type":"literal", "text":":"},
			  {"name":".", "type":"number"}
			  ],
			"while":[
			  {"type":"literal", "text":", "}
			]
       		   }% b %w:word%
'
execute 'a 1:2, 3:4, 5:6, 7:8 b test'
assert_output_json_eq '{ "w": "test", "numbers": [ "2", "4", "6", "8" ] }'

cleanup_tmp_files
