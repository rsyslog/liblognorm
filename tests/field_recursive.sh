# added 2014-11-26 by singh.janmejay
# This file is part of the liblognorm project, released under ASL 2.0

source ./exec.sh $0 "recursive parsing field"

add_rule 'rule=:%word:word% %next:recursive%'
add_rule 'rule=:%word:word%'
execute '123 abc 456 def'
assert_output_json_eq '{"word": "123", "next": {"word": "abc", "next": {"word": "456", "next" : {"word": "def"}}}}'

reset_rules

debug=on
add_rule 'rule=:%subnet_addr:ipv4%/%subnet_mask:number%%tail:rest%'
add_rule 'rule=:%ip_addr:ipv4%%tail:rest%'
add_rule 'rule=:blocked inbound via: %via_ip:ipv4% from: %addresses:tokenized:, :recursive% to %server_ip:ipv4%'
execute 'blocked inbound via: 192.168.1.1 from: 1.2.3.4, 5.6.16.0/12, 8.9.10.11, 12.13.14.15, 16.17.18.0/8, 19.20.21.24/3 to 192.168.1.5'
assert_output_json_eq '{
"addresses": [
  {"ip_addr": "1.2.3.4"}, 
  {"subnet_addr": "5.6.16.0", "subnet_mask": "12"}, 
  {"ip_addr": "8.9.10.11"}, 
  {"ip_addr": "12.13.14.15"}, 
  {"subnet_addr": "16.17.18.0", "subnet_mask": "8"}, 
  {"subnet_addr": "19.20.21.24", "subnet_mask": "3"}], 
"server_ip": "192.168.1.5",
"via_ip": "192.168.1.1"}'
