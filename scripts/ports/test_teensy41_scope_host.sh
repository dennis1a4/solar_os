#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -Isrc/services tests/ports/teensy41_scope_test.c src/services/solar_os_scope_model.c \
    -lm -o /tmp/solaros-scope-model
/tmp/solaros-scope-model
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -Itests/host -Isrc -Isrc/apps -Isrc/services -Isrc/shell \
    tests/ports/teensy41_scope_app_test.c src/apps/solar_os_scope.c \
    src/services/solar_os_scope_model.c -lm -o /tmp/solaros-scope-app
/tmp/solaros-scope-app /tmp/teensy-scope-preview.svg
