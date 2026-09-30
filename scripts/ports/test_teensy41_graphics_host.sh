#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
g++ -std=c++14 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -Isrc/services -Isrc/platform/imxrt1062/teensy41 \
    tests/ports/teensy41_graphics_presenter_test.cpp -o /tmp/solaros-graphics-host
/tmp/solaros-graphics-host
