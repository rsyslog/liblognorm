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

LN="${top_builddir}/src/lognormalizer"
if [ ! -x "$LN" ]; then
    echo "lognormalizer not built, skipping parity audit"
    exit 77
fi

if ! "$LN" -oturbostrict -V >/dev/null 2>&1; then
    echo "built without TurboVM, skipping parity audit"
    exit 77
fi

LN="$LN" LN_LIBDIR="${top_builddir}/src/.libs" \
    exec python3 "${srcdir}/turbo_parity.py"
