#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
child_test_dir=$(mktemp -d /tmp/solaros-children.XXXXXX)
cc -std=c11 -O1 -g -Wall -Wextra -Werror -DSOLAR_OS_HEADLESS=1 \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -include tests/host/compat.h -Itests/host -Isrc -Isrc/services -Isrc/shell \
  -c src/solar_os.c -o "$child_test_dir/core.o"
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -DSOLAR_OS_HEADLESS=1 \
  -ffunction-sections -fdata-sections -fsanitize=address,undefined \
  -Itests/host -Isrc -Isrc/apps -Isrc/services -Isrc/shell \
  tests/ports/teensy41_children_test.cpp "$child_test_dir/core.o" \
  -Wl,--gc-sections -o "$child_test_dir/children"
"$child_test_dir/children"
