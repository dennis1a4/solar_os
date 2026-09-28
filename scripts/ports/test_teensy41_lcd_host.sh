#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
test_dir=$(mktemp -d /tmp/solaros-lcd-host.XXXXXX)
g++ -std=c++17 -g -fsanitize=undefined -fno-sanitize-recover=all \
    -Isrc/platform/imxrt1062/teensy41 \
    tests/ports/teensy41_lcd_terminal_test.cpp \
    src/platform/imxrt1062/teensy41/lcd_terminal.cpp \
    -o "$test_dir/terminal-test"
"$test_dir/terminal-test"
