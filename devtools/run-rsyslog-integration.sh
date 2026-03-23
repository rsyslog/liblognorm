#!/bin/bash
set -e

LIBLOGNORM_SRC=/rsyslog
WORKDIR=/tmp/liblognorm-rsyslog-integration
LIBLOGNORM_PREFIX="$WORKDIR/liblognorm-install"
RSYSLOG_SRC="$WORKDIR/rsyslog"
RSYSLOG_REF="${RSYSLOG_INTEGRATION_REF:-main}"

# Keep this focused on liblognorm-backed normalize modules. We skip the
# valgrind wrappers, regex-only tests, faketime tests, and mmdblookup.
RSYSLOG_TESTS='
mmnormalize_rule_from_string.sh
mmnormalize_rule_from_array.sh
mmnormalize_parsesuccess.sh
mmnormalize_variable.sh
mmnormalize_tokenized.sh
pmnormalize-basic.sh
pmnormalize-invld-rulebase.sh
pmnormalize-rule.sh
pmnormalize-rule_and_rulebase.sh
pmnormalize-neither_rule_rulebase.sh
pmnormalize-rule_invld-data.sh
'

dump_rsyslog_logs() {
	if [ ! -d "$RSYSLOG_SRC/tests" ]; then
		return
	fi

	find "$RSYSLOG_SRC/tests" -maxdepth 1 -name '*.log' -print | while read -r logfile; do
		printf '\n===== %s =====\n' "$logfile"
		tail -n 200 "$logfile" || true
	done

	if [ -f "$RSYSLOG_SRC/tests/test-suite.log" ]; then
		printf '\n===== %s =====\n' "$RSYSLOG_SRC/tests/test-suite.log"
		tail -n 200 "$RSYSLOG_SRC/tests/test-suite.log" || true
	fi
}

printf 'building liblognorm from %s\n' "$LIBLOGNORM_SRC"
printf 'using rsyslog ref %s\n' "$RSYSLOG_REF"

rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"

cd "$LIBLOGNORM_SRC"
autoreconf -fvi
./configure --prefix="$LIBLOGNORM_PREFIX"
make
make install

export PKG_CONFIG_PATH="$LIBLOGNORM_PREFIX/lib/pkgconfig:$LIBLOGNORM_PREFIX/lib64/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$LIBLOGNORM_PREFIX/lib:$LIBLOGNORM_PREFIX/lib64:${LD_LIBRARY_PATH:-}"
export ABORT_ALL_ON_TEST_FAIL=YES

git clone --depth 1 --branch "$RSYSLOG_REF" https://github.com/rsyslog/rsyslog.git "$RSYSLOG_SRC"

cd "$RSYSLOG_SRC"
RSYSLOG_CONFIGURE_ARGS=(
	--enable-testbench
	--enable-mmnormalize
	--enable-pmnormalize
	--enable-imptcp
	--disable-default-tests
	--disable-libfaketime
	--without-valgrind-testbench
)
./autogen.sh "${RSYSLOG_CONFIGURE_ARGS[@]}"

set +e
make -j1 check TESTSUITEFLAGS=--stop TESTS="$RSYSLOG_TESTS"
rc=$?
set -e

if [ "$rc" -ne 0 ]; then
	dump_rsyslog_logs
fi

exit "$rc"
