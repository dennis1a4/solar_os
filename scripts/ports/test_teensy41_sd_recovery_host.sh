#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
g++ -std=c++14 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -Isrc/platform/imxrt1062/teensy41 \
    tests/ports/teensy41_sd_recovery_test.cpp -o /tmp/solaros-sd-recovery-host
/tmp/solaros-sd-recovery-host
