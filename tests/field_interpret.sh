# added 2014-12-11 by singh.janmejay
# This file is part of the liblognorm project, released under ASL 2.0
source ./exec.sh $0 "value interpreting field"

add_rule 'rule=:rule=:%session_count:interpret:int:word% sessions established'
execute '64 sessions established'
assert_output_json_eq '{"sessions_count": 64}'

reset_rules
add_rule 'rule=:record count for shard [%shard:interpret:base16int:char-to:]%] is %record_count:interpret:base10int:word% and %latency_percentile:interpret:float:word%\x37ile latency is %latency:interpret:float:word% %latency_unit:word%'
execute 'record count for shard [3F] is 50000 and 99.99%ile latency is 2.1 seconds'
assert_output_json_eq '{"shard": 63, "record_count": 50000, "latency_percentile": 99.99, "latency": 2.1, "latency_unit" : "seconds"}'


