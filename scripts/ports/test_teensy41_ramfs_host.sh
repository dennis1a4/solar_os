#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
ramfs_test_dir=$(mktemp -d /tmp/solaros-ramfs.XXXXXX)
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -DSOLAR_OS_RAMFS_PORTABLE=1 -DSOLAR_OS_BOARD_HAS_PSRAM=1 \
  -include tests/host/compat.h -Itests/ports/ramfs_stubs -Itests/host -Iinclude -Isrc -Isrc/services \
  tests/ports/teensy41_ramfs_test.c src/services/solar_os_ramfs.c -o "$ramfs_test_dir/test"
"$ramfs_test_dir/test"
