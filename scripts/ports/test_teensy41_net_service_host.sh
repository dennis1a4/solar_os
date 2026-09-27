#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
net_test_dir=$(mktemp -d /tmp/solaros-net-service.XXXXXX)
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
  -DSOLAR_OS_NET_PORT_TRANSPORT=1 '-DtaskENTER_CRITICAL()=((void)0)' \
  '-DtaskEXIT_CRITICAL()=((void)0)' -include tests/host/compat.h \
  -Isrc/platform/imxrt1062/teensy41/compat -Itests/host -Itests/host/freertos -Isrc/services \
  tests/host/net_session_port_test.c src/services/solar_os_net_session.c \
  -o "$net_test_dir/session"
"$net_test_dir/session"
