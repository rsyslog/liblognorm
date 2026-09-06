#!/bin/bash
# TurboVM vs standard-parser field-type parity
# This file is part of the liblognorm project, released under ASL 2.0
#
# Runs tests/turbo_parity.py, which compares every field type on both engines
# with the turbo side in STRICT mode (-oturbostrict, no walker fallback).
# Without that, a turbo defect is invisible: the walker quietly produces the
# right answer and every comparison passes. This is the only test that covers
# the type matrix, so it belongs in make check rather than being run by hand.
srcdir="${srcdir:-.}"
top_builddir="${top_builddir:-..}"

if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 not available, skipping parity audit"
    exit 77
fi

# ln_test rather than lognormalizer: both are built from the same sources, but
# ln_test links with -no-install, so it binds the library in this build tree.
# The lognormalizer libtool wrapper places the system library directory ahead
# of src/.libs, so on a host that already carries an installed liblognorm the
# audit compares that library against itself and reports its defects as ours.
LN="${top_builddir}/src/ln_test"
if [ ! -x "$LN" ]; then
    echo "ln_test not built, skipping parity audit"
    exit 77
fi

if ! "$LN" -oturbostrict -V >/dev/null 2>&1; then
    echo "built without TurboVM, skipping parity audit"
    exit 77
fi

LN="$LN" LN_LIBDIR="${top_builddir}/src/.libs" \
    exec python3 "${srcdir}/turbo_parity.py"
