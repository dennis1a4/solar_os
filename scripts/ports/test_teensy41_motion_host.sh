#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
motion_test_dir=$(mktemp -d /tmp/solaros-motion.XXXXXX)
trap 'rm -rf "$motion_test_dir"' EXIT
for motion_enabled in 0 1; do
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -DSK_MOTION="$motion_enabled" \
    -Isrc/platform/imxrt1062/teensy41 tests/ports/teensy41_motion_test.cpp \
    -o "$motion_test_dir/test"
"$motion_test_dir/test"
done
