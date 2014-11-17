# added 2014-11-14 by singh.janmejay
# This file is part of the rsyslog project, released under ASL 2.0
desc="type default for regex field"
source exec.sh
echo 'foo bar' | execute
assert_output_is '{ "first" : "foo", "second" : "bar" }'

