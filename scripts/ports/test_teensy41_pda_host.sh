#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
pda_test_dir=$(mktemp -d /tmp/solaros-pda.XXXXXX)
trap 'rm -rf "$pda_test_dir"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc/services tests/ports/teensy41_pda_model_test.c src/services/solar_os_{organizer_model,organizer_io,units,puzzles}.c -lm -o "$pda_test_dir/model"
"$pda_test_dir/model"
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/host -Isrc -Isrc/apps -Isrc/services -Isrc/shell tests/ports/teensy41_pda_ui_test.c src/apps/solar_os_{organizer_apps,convert,library,puzzle_apps,writer_files}.c src/services/solar_os_{organizer_model,units,puzzles}.c -lm -o "$pda_test_dir/ui"
"$pda_test_dir/ui"
