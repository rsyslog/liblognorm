#!/bin/bash
# Ensure the callback test uses the freshly built liblognorm from the tree.

if [ -x "./err_callback_cookie" ] && [ -d "../src/.libs" ]; then
	test_bin="./err_callback_cookie"
	build_libdir="../src/.libs"
else
	script_dir="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
	top_builddir="${top_builddir:-${script_dir}/..}"
	test_bin="${top_builddir}/tests/err_callback_cookie"
	build_libdir="${top_builddir}/src/.libs"
fi

if [ -n "${LD_LIBRARY_PATH}" ]; then
	export LD_LIBRARY_PATH="${build_libdir}:${LD_LIBRARY_PATH}"
else
	export LD_LIBRARY_PATH="${build_libdir}"
fi

exec "${test_bin}"
