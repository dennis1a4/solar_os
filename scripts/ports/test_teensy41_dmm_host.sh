#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
dmm_test_dir=$(mktemp -d /tmp/solaros-dmm-host.XXXXXX)
trap 'rm -rf -- "$dmm_test_dir"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
  -Isrc/platform/imxrt1062/teensy41 tests/ports/teensy41_dmm_model_test.cpp \
  -o "$dmm_test_dir/model"
"$dmm_test_dir/model"
python3 tests/ports/test_teensy41_dmm_render.py
