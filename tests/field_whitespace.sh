#!/bin/bash
# added 2015-03-12 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh
no_solaris10

test_def $0 "whitespace parser"
# the "word" parser unfortunatly treats everything except
# a SP as being in the word. So a HT inside a word is
# permitted, which does not work well with what we
# want to test here. to solve this problem, we use op-quoted-string.
# However, we must actually quote the samples with HT, because
# that parser also treats HT as being part of the word. But thanks
# to the quotes, we can force it to not do that.
# rgerhards, 2015-04-30
add_rule 'version=2'
add_rule 'rule=:%a:op-quoted-string%%-:whitespace%%b:op-quoted-string%'

execute 'word1  word2' # multiple spaces
assert_output_json_eq '{ "b": "word2", "a": "word1" }'
execute 'word1 word2' # single space
assert_output_json_eq '{ "b": "word2", "a": "word1" }'
execute '"word1"	"word2"' # tab (US-ASCII HT)
assert_output_json_eq '{ "b": "word2", "a": "word1" }'
execute '"word1"	   	"word2"' # mix of tab and spaces
assert_output_json_eq '{ "b": "word2", "a": "word1" }'


cleanup_tmp_files

