#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
motion_test_dir=$(mktemp -d /tmp/solaros-motion.XXXXXX)
trap 'rm -rf "$motion_test_dir"' EXIT
for motion_enabled in 0 1; do
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -DSK_MOTION="$motion_enabled" \
    -Itests/host -Isrc/services -Isrc/platform/imxrt1062/teensy41 tests/ports/teensy41_motion_test.cpp \
    -o "$motion_test_dir/test"
"$motion_test_dir/test"
done
for service in imu mag stream; do
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -include tests/host/compat.h -Itests/host -Isrc/services \
    -c "src/services/solar_os_${service}.c" -o "$motion_test_dir/$service.o"
done
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -Itests/ports/motion_stubs -Itests/host -Isrc/services \
    -Isrc/platform/imxrt1062/teensy41 tests/ports/teensy41_motion_service_test.cpp \
    "$motion_test_dir/imu.o" "$motion_test_dir/mag.o" "$motion_test_dir/stream.o" \
    -o "$motion_test_dir/services"
"$motion_test_dir/services"
