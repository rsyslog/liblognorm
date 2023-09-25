#!/bin/bash
# added 2023-09-25 by KGuillemot
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh
no_solaris10

test_def $0 "v3 name-value-list parser"

add_rule 'version=3'
add_rule 'rule=:<%syslog!priority:number%>%received!time:date-rfc3164% %host!name:word% %log!data:name-value-list%'

execute '<15>Feb 10 08:30:07 hostname a=b c=d y=z'
assert_output_json_eq '{ "log": { "data": { "a": "b", "c": "d", "y": "z" } }, "host": { "name": "hostname" }, "received": { "time": "Feb 10 08:30:07" }, "syslog": { "priority": "15" } }'

cleanup_tmp_files

