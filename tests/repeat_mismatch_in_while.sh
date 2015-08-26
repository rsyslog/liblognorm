# added 2015-08-26 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
# This is based on a practical support case, see
# https://github.com/rsyslog/liblognorm/issues/130

. $srcdir/exec.sh

test_def $0 "repeat with mismatch in while part"

reset_rules
add_rule 'version=2'
add_rule 'prefix=%timestamp:date-rfc3164% %hostname:word%'
add_rule 'rule=cisco,fwblock: \x25ASA-6-106015\x3a Deny %proto:word% (no connection) from %source:cisco-interface-spec% to %dest:cisco-interface-spec% flags %flags:repeat{ "parser": {"type":"word", "name":"."}, "while":{"type":"literal", "text":" "} }% on interface %srciface:word%'

execute 'Aug 18 13:18:45 192.168.99.2 %ASA-6-106015: Deny TCP (no connection) from 173.252.88.66/443 to 76.79.249.222/52746 flags RST  on interface outside'
assert_output_json_eq '{ "originalmsg": "Aug 18 13:18:45 192.168.99.2 %ASA-6-106015: Deny TCP (no connection) from 173.252.88.66\/443 to 76.79.249.222\/52746 flags RST  on interface outside", "unparsed-data": "RST  on interface outside" }'


cleanup_tmp_files
