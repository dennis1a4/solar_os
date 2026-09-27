#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
settings_test_dir=$(mktemp -d /tmp/solaros-settings-build.XXXXXX)
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -DSK_SETTINGS=1 '-DSK_SETTINGS_DIR="settings"' \
  -Isrc/platform/imxrt1062/teensy41/compat -Isrc/services \
  src/platform/imxrt1062/teensy41/settings.c tests/ports/teensy41_settings_test.c \
  -o "$settings_test_dir/settings"
"$settings_test_dir/settings"
