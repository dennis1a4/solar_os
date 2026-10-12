#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
c++ -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
 -Itests/ports/wireless_stubs -Itests/host -Isrc/services \
 tests/ports/teensy41_wireless_test.cpp -o /tmp/teensy-wireless-host
/tmp/teensy-wireless-host

/tmp/teensy-wireless-host slot1
