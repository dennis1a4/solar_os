#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
history_test_dir=$(mktemp -d /tmp/solaros-history-build.XXXXXX)
python3 - "$history_test_dir" <<'PY'
from pathlib import Path
import sys
source=Path('src/apps/solar_os_shell.c').read_text()
start=source.index('static bool shell_history_add_ram(')
end=source.index('\n#if SK_SETTINGS && SK_LCD_CONSOLE\n#include "shell_history.h"',start)
Path(sys.argv[1],'history_add.inc').write_text(source[start:end])
PY
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$history_test_dir" -Isrc/platform/imxrt1062/teensy41 \
  tests/ports/teensy41_history_test.c -o "$history_test_dir/test"
"$history_test_dir/test"
