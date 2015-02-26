# added 2015-02-25 by singh.janmejay
# This file is part of the liblognorm project, released under ASL 2.0
source ./exec.sh $0 "field with one of many possible suffixes"

add_rule 'rule=:gc reclaimed %eden_free:suffixed:,:b,kb,mb,gb:number% eden [surviver: %surviver_used:suffixed:;:kb;mb;gb;b:number%/%surviver_size:suffixed:|:b|kb|mb|gb:float%]'
execute 'gc reclaimed 559mb eden [surviver: 95b/30.2mb]'
assert_output_json_eq '{"eden_free": {"value": "559", "suffix":"mb"}, "surviver_used": {"value": "95", "suffix": "b"}, "surviver_size": {"value": "30.2", "suffix": "mb"}}'

reset_rules

add_rule 'rule=:gc reclaimed %eden_free:named_suffixed:size:unit:,:b,kb,mb,gb:number% eden [surviver: %surviver_used:named_suffixed:sz:u:;:kb;mb;gb;b:number%/%surviver_size:suffixed:|:b|kb|mb|gb:float%]'
execute 'gc reclaimed 559mb eden [surviver: 95b/30.2mb]'
assert_output_json_eq '{"eden_free": {"size": "559", "unit":"mb"}, "surviver_used": {"sz": "95", "u": "b"}, "surviver_size": {"value": "30.2", "suffix": "mb"}}'
