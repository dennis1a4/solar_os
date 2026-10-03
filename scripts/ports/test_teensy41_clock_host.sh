#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
clock_test_dir=$(mktemp -d /tmp/solaros-clock-host.XXXXXX)
cc -std=c11 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -DSK_CLOCK=1 -DSK_SETTINGS=1 '-DSK_SETTINGS_DIR="settings"' \
    -include tests/host/compat.h -Isrc -Isrc/platform/imxrt1062/teensy41/compat \
    -Isrc -Isrc/apps -Isrc/services \
    tests/ports/teensy41_clock_test.c src/apps/solar_os_clock.c \
    src/platform/imxrt1062/teensy41/clock_services.c \
    src/platform/imxrt1062/teensy41/settings.c tests/ports/memory_host_stubs.c src/services/solar_os_timezone.c \
    -o "$clock_test_dir/test"
"$clock_test_dir/test" "$clock_test_dir" test
"$clock_test_dir/test" "$clock_test_dir" reload
