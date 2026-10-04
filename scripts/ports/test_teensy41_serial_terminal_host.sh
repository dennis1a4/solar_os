#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
serial_test_dir=$(mktemp -d /tmp/solaros-serial.XXXXXX)
c++ -std=c++17 -g -O1 -Wall -Wextra -Werror -Wno-misleading-indentation -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -DSK_HW_RESOURCES=1 -DSOLAR_OS_BUS_PORTABLE=1 \
  -Itests/ports/serial_stubs -Itests/ports/hardware_stubs -Itests/host -Iinclude -Isrc -Isrc/services -Isrc/shell -Isrc/apps \
  -Isrc/platform/imxrt1062/teensy41 \
  tests/ports/teensy41_serial_terminal_test.cpp -Wl,--gc-sections -o "$serial_test_dir/test"
"$serial_test_dir/test" "$serial_test_dir"
