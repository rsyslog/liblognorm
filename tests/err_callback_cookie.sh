#!/bin/bash
# Ensure the callback test uses the freshly built liblognorm from the tree.

script_dir="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
top_builddir="${top_builddir:-${script_dir}/..}"
build_libdir="${top_builddir}/src/.libs"
if [ -n "${LD_LIBRARY_PATH}" ]; then
	export LD_LIBRARY_PATH="${build_libdir}:${LD_LIBRARY_PATH}"
else
	export LD_LIBRARY_PATH="${build_libdir}"
fi

exec "${top_builddir}/tests/err_callback_cookie"
