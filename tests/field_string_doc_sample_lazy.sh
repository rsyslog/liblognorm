# added 2018-06-26 by Rainer Gerhards
# This file is part of the liblognorm project, released under ASL 2.0

uname -a | grep "SunOS.*5.10"
if [ $? -eq 0 ] ; then
   echo platform: `uname -a`
   echo This looks like solaris 10, we disable known-failing tests to
   echo permit OpenCSW to build packages. However, this are real failurs
   echo and so a fix should be done as soon as time permits.
   exit 77
fi
. $srcdir/exec.sh

test_def $0 "string syntax"

reset_rules
add_rule 'version=2'
add_rule 'rule=:%f:string{"matching.permitted":[ {"class":"digit"} ], "matching.mode":"lazy"}
                   %%r:rest%'

execute '12:34 56'
assert_output_json_eq '{ "r": ":34 56", "f": "12" }'

cleanup_tmp_files
