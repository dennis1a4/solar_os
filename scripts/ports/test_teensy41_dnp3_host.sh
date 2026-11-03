#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
test_bin=$(mktemp /tmp/solaros-dnp3-test.XXXXXX)
trap 'rm -f "$test_bin"' EXIT
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc/services tests/ports/teensy41_dnp3_test.c src/services/solar_os_dnp3_lab.c src/services/solar_os_dnp3_wire.c src/vendor/opendnp3/{crc,functions,decode}.c -lm -o "$test_bin"
ASAN_OPTIONS=detect_leaks=1 "$test_bin"
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/host -Isrc -Isrc/apps -Isrc/services -Isrc/shell tests/ports/teensy41_dnp3_app_test.c src/apps/solar_os_dnp3_app.c src/services/solar_os_dnp3_lab.c src/services/solar_os_dnp3_wire.c src/vendor/opendnp3/{crc,functions,decode}.c -lm -o "$test_bin"
ASAN_OPTIONS=detect_leaks=1 "$test_bin"
