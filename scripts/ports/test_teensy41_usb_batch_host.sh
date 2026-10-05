#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
usb_batch_test_dir=$(mktemp -d /tmp/solaros-usb-batch.XXXXXX)
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Isrc/platform/imxrt1062/teensy41 tests/ports/teensy41_usb_batch_test.cpp \
  -o "$usb_batch_test_dir/test"
"$usb_batch_test_dir/test"
