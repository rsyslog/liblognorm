#!/bin/sh
# Instrument the library (rewrite.c, pdag.c, turbo_vm.c included) with
# AddressSanitizer and UndefinedBehaviorSanitizer, run the rewrite tests,
# then a short libFuzzer whose coverage is the instrumented library.
# This is not the production build. The default compile flags are unchanged.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BUILD=${TMPDIR:-/tmp}/liblognorm-rewrite-san
JOBS=${JOBS:-2}
CC=${CC:-clang}

rm -rf "$BUILD"
mkdir -p "$BUILD"
cd "$BUILD"

# fuzzer-no-link instruments rewrite.c, pdag.c and turbo_vm.c for the
# coverage map. It does not pull libFuzzer's main into the test binaries.
CFLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined,fuzzer-no-link -fno-sanitize-recover=undefined"
LDFLAGS="-fsanitize=address,undefined,fuzzer-no-link"
# Cap the quarantine. A fuzzer that allocates and frees a JSON object per
# input otherwise counts the quarantine as a leak and trips its RSS limit.
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:detect_leaks=0:quarantine_size_mb=16}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

# configure is not in git. A fresh checkout has to bootstrap it.
( cd "$ROOT" && autoreconf -fvi )
"$ROOT/configure" --enable-turbo CC="$CC" CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS"
make -j"$JOBS"
make -C tests rewrite_test rewrite_fuzz
( cd tests && ./rewrite_test && ./rewrite_fuzz )

# Link libFuzzer against the instrumented shared library. Skip when this
# clang has no libFuzzer (the deterministic rewrite_fuzz above still ran).
FUZZ_PROBE=$BUILD/fuzz-probe
if printf '%s\n' 'int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long n){(void)d;(void)n;return 0;}' \
	| "$CC" -fsanitize=fuzzer,address,undefined -x c - -o "$FUZZ_PROBE" >/dev/null 2>&1
then
	rm -f "$FUZZ_PROBE"
	# shellcheck disable=SC2046
	"$CC" -fsanitize=fuzzer,address,undefined -DREWRITE_LIBFUZZER \
		-I"$ROOT" -I"$ROOT/src" -I"$BUILD" -I"$BUILD/src" \
		$(pkg-config --cflags libfastjson libestr) \
		"$ROOT/tests/rewrite_fuzz.c" \
		-L"$BUILD/src/.libs" -llognorm \
		$(pkg-config --libs libfastjson libestr) \
		-o "$BUILD/rewrite_lf"
	LD_LIBRARY_PATH="$BUILD/src/.libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
		"$BUILD/rewrite_lf" -max_total_time=15 -max_len=512 -rss_limit_mb=400
else
	echo "libFuzzer not linked by this clang; rewrite_fuzz covered the same oracle"
fi

echo "rewrite sanitizers: ok"
