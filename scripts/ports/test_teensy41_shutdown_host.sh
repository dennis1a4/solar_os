#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
shutdown_test_dir=$(mktemp -d /tmp/solaros-shutdown.XXXXXX)
c++ -std=c++17 -g -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -DSK_UPSTREAM_SHELL=1 -DSK_HW_RESOURCES=1 -DSK_LCD_CONSOLE=1 -DSK_POWER=1 \
  -Itests/ports/power_shutdown_stubs -Itests/host -Iinclude -Isrc -Isrc/services -Isrc/shell -Isrc/apps \
  -Isrc/platform/imxrt1062/teensy41 tests/ports/teensy41_shutdown_test.cpp \
  -Wl,--gc-sections -o "$shutdown_test_dir/test"
"$shutdown_test_dir/test"
