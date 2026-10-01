#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
syntax_test_dir=$(mktemp -d /tmp/solaros-syntax.XXXXXX)
cc -std=c11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -Isrc/services tests/host/syntax_test.c src/services/solar_os_syntax.c -o "$syntax_test_dir/test"
"$syntax_test_dir/test"
