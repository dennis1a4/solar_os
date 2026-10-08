#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
hardware_test_dir=$(mktemp -d /tmp/solaros-hardware.XXXXXX)
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -include tests/host/compat.h -Itests/host -Isrc/services \
  -c src/services/solar_os_resources.c -o "$hardware_test_dir/resources.o"
for wiring in legacy display; do
if [[ "$wiring" == legacy ]]; then
  display_flags=(-DSK_PRIMARY_CS=37 -DSK_PRIMARY_RESET=9)
else
  display_flags=(-DSK_PRIMARY_CS=10 -DSK_PRIMARY_RESET=14 -DSK_PRIMARY_WAIT=-1 -DSK_PRIMARY_BACKLIGHT=15 -DSK_MOTOR_ENABLE_PIN=-1 -DSK_SECONDARY_ST7735=1 -DSK_SECONDARY_SPI_BUS=0)
fi
c++ -std=c++17 -g -O1 -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined \
  -DSK_HW_RESOURCES=1 -DSOLAR_OS_BUS_PORTABLE=1 -DSK_PRIMARY_RA8875=1 "${display_flags[@]}" \
  -DSK_AUDIO_SGTL5000=1 -DSK_UART_CONSOLE=1 -DSK_AMPLIFIER_SHUTDOWN_PIN=40 \
  -Itests/ports/hardware_stubs -Itests/host -Iinclude -Isrc -Isrc/services -Isrc/shell \
  -Isrc/platform/imxrt1062/teensy41 \
  tests/ports/teensy41_hardware_test.cpp src/platform/imxrt1062/teensy41/buses.cpp \
  src/platform/imxrt1062/teensy41/hardware.cpp "$hardware_test_dir/resources.o" -o "$hardware_test_dir/test"
"$hardware_test_dir/test"
done
