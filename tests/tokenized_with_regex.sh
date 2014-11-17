# added 2014-11-17 by singh.janmejay
# This file is part of the liblognorm project, released under ASL 2.0
source ./exec.sh $0 "tokenized field with regex based field"
add_rule 'rule=:%parts:tokenized:,:regex:[^, ]+% %text:rest%'
execute '123,abc,456,def foo bar'
assert_output_contains '"parts": [ "123", "abc", "456", "def" ]'
assert_output_contains '"text": "foo bar"'
