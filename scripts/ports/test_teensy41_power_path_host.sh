#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
power_path_test_dir=$(mktemp -d /tmp/solaros-power-path.XXXXXX)
trap 'rm -rf "$power_path_test_dir"' EXIT
c++ -std=c++17 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -DSK_POWER_PATH=1 -Isrc/platform/imxrt1062/teensy41 \
    tests/ports/teensy41_power_path_test.cpp src/platform/imxrt1062/teensy41/power_path_model.cpp \
    -o "$power_path_test_dir/power-path"
"$power_path_test_dir/power-path"
