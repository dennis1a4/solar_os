#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
completion_test_dir=$(mktemp -d /tmp/solaros-completion-test.XXXXXX)
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Isrc/services src/services/solar_os_completion.c src/services/solar_os_completion_files.c \
  tests/host/completion_test.c -o "$completion_test_dir/test"
"$completion_test_dir/test"
