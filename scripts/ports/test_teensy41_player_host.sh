#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
player_test_dir=$(mktemp -d /tmp/solaros-player-test.XXXXXX)
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
    -DSK_WEBRADIO=1 -Itests/host -Isrc -Isrc/services -Isrc/platform/imxrt1062/teensy41 \
    tests/ports/teensy41_player_folder_test.c src/platform/imxrt1062/teensy41/player_folder.c \
    -o "$player_test_dir/folder"
"$player_test_dir/folder"
