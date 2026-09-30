#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -Isrc/services tests/ports/teensy41_power_test.c src/services/solar_os_pd_power.c \
    -o /tmp/solaros-power-host
/tmp/solaros-power-host
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -Itests/host -Isrc -Isrc/apps -Isrc/services -Isrc/shell \
    tests/ports/teensy41_power_app_test.c src/apps/solar_os_power_app.c \
    src/services/solar_os_pd_power.c -o /tmp/solaros-power-app-host
/tmp/solaros-power-app-host
