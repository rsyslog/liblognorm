# added 2014-11-26 by singh.janmejay
# This file is part of the liblognorm project, released under ASL 2.0

source ./exec.sh $0 "recursive parsing field"

add_rule 'rule=:%word:word% %next:recursive%'
add_rule 'rule=:%word:word%'
execute '123 abc 456 def'
assert_output_json_eq '{"word": "123", "next": {"word": "abc", "next": {"word": "456", "next" : {"word": "def"}}}}'
