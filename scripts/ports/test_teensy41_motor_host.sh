#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
for test in motor usb_host_power; do
  c++ -std=c++17 -Wall -Wextra -Werror -Wno-misleading-indentation -O1 -g \
    -fsanitize=address,undefined -Itests/ports/motor_stubs \
    -Itests/ports/wireless_stubs -Itests/host -Isrc/services \
    "tests/ports/teensy41_${test}_test.cpp" -o "/tmp/teensy-${test}-host"
  ASAN_OPTIONS=detect_leaks=0 "/tmp/teensy-${test}-host"
done
