#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
small_test_dir=$(mktemp -d /tmp/solaros-small-pages.XXXXXX)
c++ -std=c++17 -Wall -Wextra -Werror \
    -Isrc/platform/imxrt1062/teensy41 tests/ports/teensy41_small_navigation_test.cpp \
    -o "$small_test_dir/test"
"$small_test_dir/test"
c++ -std=c++17 -Wall -Wextra -Werror \
    -Isrc/platform/imxrt1062/teensy41 tests/ports/teensy41_small_monitor_row_test.cpp \
    -o "$small_test_dir/rows"
"$small_test_dir/rows"
