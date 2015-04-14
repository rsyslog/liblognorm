# added 2015-04-13 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0
source ./exec.sh $0 "cisco-interface-spec syntax"
add_rule 'rule=:begin %field:cisco-interface-spec% end'

execute 'begin outside:176.97.252.102/50349 end'
assert_output_json_eq '{"field": "outside:176.97.252.102/50349"}'

execute 'begin outside:176.97.252.102/50349(DOMAIN\rainer) end'
# we need to add the backslash escape for the testbench plumbing
assert_output_json_eq '{"field": "outside:176.97.252.102/50349(DOMAIN\\rainer)"}'

execute 'begin outside:176.97.252.102/50349(test/rainer) end'
# we need to add the backslash escape for the testbench plumbing
assert_output_json_eq '{"field": "outside:176.97.252.102/50349(test/rainer)"}'

execute 'begin outside:176.97.252.102/50349(rainer) end'
# we need to add the backslash escape for the testbench plumbing
assert_output_json_eq '{"field": "outside:176.97.252.102/50349(rainer)"}'
