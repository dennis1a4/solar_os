#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
netdiag_test_dir=$(mktemp -d /tmp/solaros-netdiag.XXXXXX)
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation \
  -fsanitize=address,undefined -include initializer_list -Isrc \
  tests/ports/teensy41_netdiag_test.cpp -o "$netdiag_test_dir/test"
"$netdiag_test_dir/test"
