#!/bin/bash
# script for build-only CI runs via container
printf 'running build with\n'
printf 'container: %s\n' "$LIBLOGNORM_DEV_CONTAINER"
printf 'CC:\t%s\n' "$CC"
printf 'CFLAGS:\t%s\n' "$CFLAGS"
printf 'LDFLAGS:\t%s\n' "$LDFLAGS"
printf 'working directory: %s\n' "$(pwd)"
printf 'user ids: %s:%s\n' "$(id -u)" "$(id -g)"

set -e

if [ -n "$LIBLOGNORM_CONFIGURE_OPTIONS_OVERRIDE" ]; then
	CONFIGURE_OPTS="$LIBLOGNORM_CONFIGURE_OPTIONS_OVERRIDE"
else
	CONFIGURE_OPTS="$LIBLOGNORM_CONFIGURE_OPTIONS_EXTRA"
fi

printf 'CONFIGURE_OPTS:\t%s\n' "$CONFIGURE_OPTS"

printf 'STEP: autoreconf / configure ===============================================\n'
autoreconf -fvi
# shellcheck disable=SC2086
./configure $CONFIGURE_OPTS

printf 'STEP: make =================================================================\n'
make ${CI_MAKE_OPT:-}
