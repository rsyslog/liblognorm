#!/bin/bash
set -e
cd /rsyslog

echo "SCAN_BUILD_CC: $SCAN_BUILD_CC"
echo "SCAN_BUILD: $SCAN_BUILD"

if [ -n "$SCAN_BUILD_REPORT_DIR" ]; then
	export CURR_REPORT="$(date +%y-%m-%d_%H-%M-%S)"
	export REPORT_DIR="$SCAN_BUILD_REPORT_DIR/$CURR_REPORT"
fi

autoreconf -fvi

export CC="${SCAN_BUILD_CC:-clang}"
./configure \
	${LIBLOGNORM_CONFIGURE_OPTIONS_OVERRIDE:-} \
	${LIBLOGNORM_CONFIGURE_OPTIONS_EXTRA:-}

set +e
if [ -n "$REPORT_DIR" ]; then
	"${SCAN_BUILD:-scan-build}" -o "$REPORT_DIR" --use-cc "$CC" --status-bugs make -j2
else
	"${SCAN_BUILD:-scan-build}" --use-cc "$CC" --status-bugs make -j2
fi
RESULT=$?
set -e

if [ "$RESULT" -eq 1 ]; then
	echo "scan-build failed"
	if [ -n "$SCAN_BUILD_REPORT_DIR" ]; then
		echo "scan-build report URL: ${SCAN_BUILD_REPORT_BASEURL}${CURR_REPORT}" > report_url
	fi
fi

echo "static analyzer result: $RESULT"
exit "$RESULT"
