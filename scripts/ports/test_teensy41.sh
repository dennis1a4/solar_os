#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
# Generated artifacts stay outside the source tree.
sk_test_dir=$(mktemp -d /tmp/solar-teensy-tests.XXXXXX)
trap 'rm -rf -- "$sk_test_dir"' EXIT
cc -std=c11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE \
  -DSOLAR_OS_HEADLESS=1 -ffunction-sections -fdata-sections \
  -include tests/host/compat.h -Itests/host -Isrc -Isrc/services -Isrc/shell \
  tests/ports/teensy41_core_test.c src/solar_os.c \
  src/shell/solar_os_shell_parse.c src/services/solar_os_expr.c \
  -Wl,--gc-sections -lm -o "$sk_test_dir/core"
"$sk_test_dir/core"
cc -std=c11 -O2 -Wall -Wextra -Werror -Isrc/shell \
  tests/host/shell_parse_test.c src/shell/solar_os_shell_parse.c -o "$sk_test_dir/parser"
"$sk_test_dir/parser"
cc -std=c11 -O2 -Wall -Wextra -Werror -Isrc/shell \
  tests/host/shell_line_test.c src/shell/solar_os_shell_line.c -o "$sk_test_dir/line"
"$sk_test_dir/line"
cc -std=c11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE \
  -ffunction-sections -fdata-sections -include tests/host/compat.h \
  -Itests/host -Isrc -Isrc/services -Isrc/shell -Icomponents/u8g2/src/clib \
  tests/host/app_exit_result_test.c src/solar_os.c src/shell/solar_os_shell_io.c \
  -Wl,--gc-sections -o "$sk_test_dir/context"
"$sk_test_dir/context"

cc -std=c11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE -DSK_UPSTREAM_SHELL=1 \
  -include tests/host/compat.h -Itests/host -Isrc/services \
  tests/ports/teensy41_paths_test.c src/platform/imxrt1062/teensy41/shell_paths.c \
  -o "$sk_test_dir/paths"
"$sk_test_dir/paths"

# Render actual calculator output; text-only transcript checks miss cursor bugs.
cc -std=c11 -O2 -Wall -Wextra -D_GNU_SOURCE -DSOLAR_OS_HEADLESS=1 \
  -ffunction-sections -fdata-sections -include tests/host/compat.h \
  -Itests/host -Isrc -Isrc/apps -Isrc/services -Isrc/shell -Icomponents/u8g2/src/clib \
  tests/ports/teensy41_cursor_test.c src/solar_os.c src/apps/solar_os_calc.c \
  src/services/solar_os_expr.c src/shell/solar_os_shell_io.c \
  -Wl,--gc-sections -lm -o "$sk_test_dir/cursor"
"$sk_test_dir/cursor"
