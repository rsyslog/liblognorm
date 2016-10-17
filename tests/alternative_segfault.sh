# added 2016-10-17 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0

. $srcdir/exec.sh

test_def $0 "a case that caused a segfault in practice"
add_rule 'version=2'
add_rule 'rule=:%host:ipv4% %{"type":"alternative","parser":[{"type":"literal","text":"-"},{"type":"word","name":"identd"}]}% %r:rest%'
execute '1.2.3.4 - - [23/Sep/2016:11:12:50 +0200] \"GET /app/img/oldlogo.png HTTP/1.1\" 304 -'
assert_output_json_eq '{ "hex": "0x4711" }'

cleanup_tmp_files
