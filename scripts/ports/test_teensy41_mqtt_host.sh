#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -Itests/host -Isrc/services tests/ports/teensy41_mqtt_test.c \
    src/services/solar_os_mqtt_wire.c src/services/solar_os_mqtt_capture_model.c \
    -o /tmp/solaros-mqtt-host
/tmp/solaros-mqtt-host
