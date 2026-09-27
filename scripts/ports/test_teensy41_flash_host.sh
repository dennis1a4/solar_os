#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
flash_test_dir=$(mktemp -d /tmp/solaros-flash.XXXXXX)
flash_library="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-arduinoteensy/libraries/LittleFS/src"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
  -DSK_QSPI_FLASH=1 \
  -Isrc/platform/imxrt1062/teensy41 -I"$flash_library" \
  tests/ports/teensy41_flash_policy_test.c src/platform/imxrt1062/teensy41/flash_policy.c \
  "$flash_library/littlefs/lfs.c" "$flash_library/littlefs/lfs_util.c" \
  -Wl,--wrap=lfs_format -o "$flash_test_dir/policy"
"$flash_test_dir/policy"
