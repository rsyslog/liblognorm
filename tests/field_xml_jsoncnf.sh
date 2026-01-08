#!/bin/bash
# added 2021-11-14 by Theo Bertin
# This file is part of the liblognorm project, released under ASL 2.0
. $srcdir/exec.sh

test_def $0 "XML field"
add_rule 'version=2'
add_rule 'rule=:%{"name":"field", "type":"xml"}%'

execute '<?xml version="1.0" encoding="UTF-8"?><note>This is a simple note</note>'
assert_output_json_eq '{ "field": { "note": "This is a simple note"} }'

execute '<?xml version="1.0" encoding="UTF-8"?><note><one>first note</one><two>second note</two></note>'
assert_output_json_eq '{ "field": { "note": { "one": "first note", "two": "second note" } } }'

# execute '@cee: {"f1": "1", "f2": 2}'
# assert_output_json_eq '{ "field": { "f1": "1", "f2": 2 } }'

# execute '@cee:     {"f1": "1", "f2": 2}'
# assert_output_json_eq '{ "field": { "f1": "1", "f2": 2 } }'

#
# Things that MUST NOT work
#
execute '<?xml version="1.0" encoding="UTF-8"?><note>This is a simple note</note> ' # note the trailing space
assert_output_json_eq '{ "originalmsg": "<?xml version=\"1.0\" encoding=\"UTF-8\"?><note>This is a simple note<\/note> ", "unparsed-data": " " }'

execute '<?xml version="1.0" encoding="UTF-8"?><note>This is a simple note'
assert_output_json_eq '{ "originalmsg": "<?xml version=\"1.0\" encoding=\"UTF-8\"?><note>This is a simple note", "unparsed-data": "<?xml version=\"1.0\" encoding=\"UTF-8\"?><note>This is a simple note" }'

execute '<?xml version="1.0" encoding="UTF-8"?><note>This is a simple note</note2>'
assert_output_json_eq '{ "originalmsg": "<?xml version=\"1.0\" encoding=\"UTF-8\"?><note>This is a simple note</note2>", "unparsed-data": "<?xml version=\"1.0\" encoding=\"UTF-8\"?><note>This is a simple note</note2>" }'


cleanup_tmp_files

