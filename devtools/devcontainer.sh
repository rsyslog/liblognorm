#!/bin/bash
# This scripts uses an rsyslog development container to execute given
# command inside it.
# Note: command line parameters are passed as parameters to the container,
# with the notable exception that -ti, if given as first parameter, is
# passed to "docker run" itself but NOT the container.
#
# use env var DOCKER_RUN_EXTRA_OPTS to provide extra options to docker run
# command.
#
#
# TO MODIFIY BEHAVIOUR, use
# LIBLOGNORM_CONTAINER_UID, format uid:gid,
#                   to change the users container is run under
#                   set to "" to use the container default settings
#                   (no local mapping)
set -e
if [ "$1" == "--rm" ]; then
	optrm="--rm"
	shift 1
fi
if [ "$1" == "-ti" ]; then
	ti="-ti"
	shift 1
fi
# check in case -ti was in front...
if [ "$1" == "--rm" ]; then
	optrm="--rm"
	shift 1
fi

if [ "$LIBLOGNORM_HOME" == "" ]; then
	export LIBLOGNORM_HOME=$(pwd)
	echo info: LIBLOGNORM_HOME not set, using $LIBLOGNORM_HOME
fi

if [ -z "$LIBLOGNORM_DEV_CONTAINER" ]; then
	LIBLOGNORM_DEV_CONTAINER=$(cat $LIBLOGNORM_HOME/devtools/default_dev_container)
fi

printf '/rsyslog is mapped to %s \n' "$LIBLOGNORM_HOME"
printf 'using container %s\n' "$LIBLOGNORM_DEV_CONTAINER"
printf 'pulling container...\n'
printf 'user ids: %s:%s\n' $(id -u) $(id -g)
printf 'container_uid: %s\n' ${LIBLOGNORM_CONTAINER_UID--u $(id -u):$(id -g)}
printf 'container cmd: %s\n' $*
printf '\nNote: we use the RSYSLOG CONTAINERS, as such project home is /rsyslog!\n\n'
docker pull $LIBLOGNORM_DEV_CONTAINER
docker run $ti $optrm $DOCKER_RUN_EXTRA_OPTS \
	-e LIBLOGNORM_CONFIGURE_OPTIONS_EXTRA \
	-e LIBLOGNORM_CONFIGURE_OPTIONS_OVERRIDE \
	-e CC \
	-e CFLAGS \
	-e LDFLAGS \
	-e LSAN_OPTIONS \
	-e TSAN_OPTIONS \
	-e UBSAN_OPTIONS \
	-e CI_MAKE_OPT \
	-e CI_MAKE_CHECK_OPT \
	-e CI_CHECK_CMD \
	-e CI_BUILD_URL \
	-e CI_CODECOV_TOKEN \
	-e CI_VALGRIND_SUPPRESSIONS \
	-e CI_SANITIZE_BLACKLIST \
	-e ABORT_ALL_ON_TEST_FAIL \
	-e USE_AUTO_DEBUG \
	-e LIBLOGNORM_STATSURL \
	-e VCS_SLUG \
	--cap-add SYS_ADMIN \
	--cap-add SYS_PTRACE \
	${LIBLOGNORM_CONTAINER_UID--u $(id -u):$(id -g)} \
	$DOCKER_RUN_EXTRA_FLAGS \
	-v "$LIBLOGNORM_HOME":/rsyslog $LIBLOGNORM_DEV_CONTAINER $*
