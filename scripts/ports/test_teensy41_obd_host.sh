#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -Isrc/services tests/ports/teensy41_obd_test.c \
    src/services/solar_os_can.c src/services/solar_os_isotp.c \
    src/services/solar_os_obd.c src/services/solar_os_obd_demo.c \
    -o /tmp/solaros-obd-host
/tmp/solaros-obd-host
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -Itests/host -Isrc -Isrc/apps -Isrc/services -Isrc/shell \
    tests/ports/teensy41_obd_app_test.c src/apps/solar_os_obd_app.c \
    src/services/solar_os_can.c src/services/solar_os_isotp.c \
    src/services/solar_os_obd.c src/services/solar_os_obd_demo.c \
    -o /tmp/solaros-obd-app-host
/tmp/solaros-obd-app-host
