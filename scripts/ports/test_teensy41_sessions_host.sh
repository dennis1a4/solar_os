#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
sessions_test_dir=$(mktemp -d /tmp/solaros-sessions.XXXXXX)
cc -std=c11 -O1 -g -Wall -Wextra -Werror -DSOLAR_OS_HEADLESS=1 \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -include tests/host/compat.h -Itests/host -Isrc -Isrc/services -Isrc/shell \
  -c src/solar_os.c -o "$sessions_test_dir/core.o"
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -DSOLAR_OS_HEADLESS=1 \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -Itests/host -Isrc -Isrc/apps -Isrc/services -Isrc/shell \
  tests/ports/teensy41_sessions_test.cpp "$sessions_test_dir/core.o" \
  -Wl,--gc-sections -o "$sessions_test_dir/sessions"
"$sessions_test_dir/sessions"
