#!/bin/bash
# Verify binary unparsed-data output through the public API.

if [ -x "./unparsed_data_binary" ] && [ -d "../src/.libs" ]; then
	test_bin="./unparsed_data_binary"
	build_libdir="../src/.libs"
else
	script_dir="$(
		CDPATH=
		cd -- "$(dirname "$0")" || exit 1
		pwd
	)"
	top_builddir="${top_builddir:-${script_dir}/..}"
	test_bin="${top_builddir}/tests/unparsed_data_binary"
	build_libdir="${top_builddir}/src/.libs"
fi

if [ -n "${LD_LIBRARY_PATH}" ]; then
	export LD_LIBRARY_PATH="${build_libdir}:${LD_LIBRARY_PATH}"
else
	export LD_LIBRARY_PATH="${build_libdir}"
fi

exec "${test_bin}"
