#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
http_test_dir=/tmp/solaros-http-host
mkdir -p "$http_test_dir"
port_dir="$PWD/src/platform/imxrt1062/teensy41"
crypto_dir="$PWD/.pio/teensy-ssh-deps/mbedtls"
make -C "$http_test_dir" -f "$PWD/tests/ports/http_crypto.mk" -j 8 \
  CRYPTO="$crypto_dir" PORT="$port_dir" > "$http_test_dir/build.log" 2>&1
c++ -std=c++17 -g -O1 -Wall -Wextra -fsanitize=address,undefined \
  -DSK_PLAYGROUND=1 "-DMBEDTLS_CONFIG_FILE=\"$port_dir/ssh_compat/teensy_mbedtls_config.h\"" \
  -Itests/ports/http_compat -I"$port_dir/compat" -Isrc -Isrc/services -I"$crypto_dir/include" \
  "$port_dir/http_client.cpp" "$port_dir/tls_roots.cpp" tests/ports/teensy41_http_test.cpp \
  "$http_test_dir/libcrypto-test.a" \
  -o "$http_test_dir/test"
"$http_test_dir/test"
