#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
compose_test_dir=$(mktemp -d /tmp/solaros-compose.XXXXXX)
flags=(-O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -ffunction-sections -fdata-sections -DSOLAR_OS_HEADLESS=1 -DSK_SHELL_COMPOSE=1 -fsanitize=address,undefined -Itests/host -Isrc -Isrc/apps -Isrc/services -Isrc/shell)
cc -std=c11 "${flags[@]}" -c src/shell/solar_os_shell_parse.c -o "$compose_test_dir/parse.o"
cc -std=c11 "${flags[@]}" -c src/shell/solar_os_shell_io.c -o "$compose_test_dir/io.o"
c++ -std=c++17 "${flags[@]}" tests/ports/teensy41_compose_test.cpp src/platform/imxrt1062/teensy41/shell_compose.cpp "$compose_test_dir/parse.o" "$compose_test_dir/io.o" -Wl,--gc-sections -o "$compose_test_dir/test"
"$compose_test_dir/test"
cc -std=c11 -D_DEFAULT_SOURCE "${flags[@]}" -Wno-format-truncation -include tests/ports/jobs_stubs/compat.h \
  tests/ports/teensy41_compose_fs_test.c src/shell/solar_os_shell_fs.c -Wl,--gc-sections -o "$compose_test_dir/fs"
"$compose_test_dir/fs"
c++ -std=c++17 "${flags[@]}" -Wno-deprecated-declarations -Itests/ports/memory_stubs \
  tests/ports/teensy41_memory_policy_test.cpp -Wl,--gc-sections -o "$compose_test_dir/memory"
"$compose_test_dir/memory"
