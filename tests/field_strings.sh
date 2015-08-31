# added 2015-04-21 by Angelo Turetta
# This file is part of the liblognorm project, released under ASL 2.0
source ./exec.sh $0 "quoted string with escapes"

logportion='"GET /www.testsite.com/RenderProducts?sQuery=true&siteCode=TEST HTTP/1.1" "http://w.tt.com/cat#{""sQuery"":""true"",""oc"":""Giant 12"",""tx"":""""}"'

add_rule 'rule=:%op-quoted:op-quoted-string% end'
execute 'test-result end'
assert_output_json_eq '{ "op-quoted": "test-result" }'
execute '"test-result" end'
assert_output_json_eq '{ "op-quoted": "test-result" }'
execute 'test-"result" end'
assert_output_json_eq '{ "op-quoted": "test-\"result\"" }'
execute 'test-\"result\" end'
assert_output_json_eq '{ "op-quoted": "test-\\\"result\\\"" }'
execute 'test-""result"" end'
assert_output_json_eq '{ "op-quoted": "test-\"\"result\"\"" }'
execute '"test-""result""" end'
assert_output_json_eq '{ "originalmsg": "\"test-\"\"result\"\"\" end", "unparsed-data": "\"result\"\"\" end" }'

reset_rules

add_rule 'rule=:%word:word% end'
execute 'test-result end'
assert_output_json_eq '{ "word": "test-result" }'
execute '"test-result" end'
assert_output_json_eq '{ "word": "\"test-result\"" }'
execute 'test-"result" end'
assert_output_json_eq '{ "word": "test-\"result\"" }'
execute 'test-\"result\" end'
assert_output_json_eq '{ "word": "test-\\\"result\\\"" }'
execute 'test-""result"" end'
assert_output_json_eq '{ "word": "test-\"\"result\"\"" }'
execute '"test-""result""" end'
assert_output_json_eq '{ "word": "\"test-\"\"result\"\"\"" }'

reset_rules

add_rule 'rule=:%str:escaped-string% end'
execute 'test-result end'
assert_output_json_eq '{ "str": "test-result" }'
execute '"test-result" end'
assert_output_json_eq '{ "str": "test-result" }'
execute 'test-"result" end'
assert_output_json_eq '{ "str": "test-result" }'
execute 'test-\"result\" end'
assert_output_json_eq '{ "str": "test-\"result\"" }'
execute 'test-""result"" end'
assert_output_json_eq '{ "str": "test-result" }'
execute '"test-""result""" end'
assert_output_json_eq '{ "str": "test-\"result\"" }'

reset_rules

add_rule 'rule=:"%-:word% /%-:char-to:/%%-:word% %-:char-to:"%" %referer:op-quoted-string% end'
execute "$logportion"' end'
assert_output_json_eq '{ "originalmsg": "\"GET \/www.testsite.com\/RenderProducts?sQuery=true&siteCode=TEST HTTP\/1.1\" \"http:\/\/w.tt.com\/cat#{\"\"sQuery\"\":\"\"true\"\",\"\"oc\"\":\"\"Giant 12\"\",\"\"tx\"\":\"\"\"\"}\" end", "unparsed-data": "\"sQuery\"\":\"\"true\"\",\"\"oc\"\":\"\"Giant 12\"\",\"\"tx\"\":\"\"\"\"}\" end" }'

reset_rules

add_rule 'rule=:"%-:word% /%-:char-to:/%%-:word% %-:char-to:"%" %referer:word% end'
execute "$logportion"' end'
assert_output_json_eq '{ "originalmsg": "\"GET \/www.testsite.com\/RenderProducts?sQuery=true&siteCode=TEST HTTP\/1.1\" \"http:\/\/w.tt.com\/cat#{\"\"sQuery\"\":\"\"true\"\",\"\"oc\"\":\"\"Giant 12\"\",\"\"tx\"\":\"\"\"\"}\" end", "unparsed-data": "12\"\",\"\"tx\"\":\"\"\"\"}\" end" }'

reset_rules

add_rule 'rule=:"%-:word% /%-:char-to:/%%-:word% %-:char-to:"%" %referer:escaped-string% end'
execute "$logportion"' end'
assert_output_json_eq '{"referer": "http:\/\/w.tt.com\/cat#{\"sQuery\":\"true\",\"oc\":\"Giant 12\",\"tx\":\"\"}"}'

#add_rule 'rule=:begin %timestamp:kernel-timestamp%'
#execute 'begin [12345.123456]'
#assert_output_json_eq '{ "timestamp": "[12345.123456]"}'

#reset_rules

