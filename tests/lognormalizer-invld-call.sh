#!/bin/bash
# This file is part of the liblognorm project, released under ASL 2.0

echo running test $0
if ../src/lognormalizer ; then
    echo "FAIL: loganalyzer did not detect missing rulebase"
    exit 1
fi
if ../src/lognormalizer -r test -R test ; then
    echo "FAIL: loganalyzer did not detect both -r and -R given"
    exit 1
fi
