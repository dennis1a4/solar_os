#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
keyboard_test_dir=$(mktemp -d /tmp/solaros-keyboard.XXXXXX)
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Isrc/platform/imxrt1062/teensy41 tests/ports/teensy41_keyboard_test.cpp \
    -o "$keyboard_test_dir/test"
"$keyboard_test_dir/test"
