# added 2015-03-12 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
source ./exec.sh $0 "whitespace parser"
add_rule 'rule=:%a:word%%-:whitespace%%b:word%'
execute 'word1  word2' # multiple spaces
assert_output_json_eq '{ "b": "word2", "a": "word1" }'
execute 'word1 word2' # single space
assert_output_json_eq '{ "b": "word2", "a": "word1" }'
execute 'word1	word2' # tab (US-ASCII HT)
assert_output_json_eq '{ "b": "word2", "a": "word1" }'
execute 'word1	   	word2' # mix of tab and spaces
assert_output_json_eq '{ "b": "word2", "a": "word1" }'
