#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
python_hardware_test_dir=$(mktemp -d /tmp/solaros-python-hardware.XXXXXX)
cc -std=gnu11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -include tests/host/compat.h -Itests/host -Isrc/services \
  -c src/services/solar_os_resources.c -o "$python_hardware_test_dir/resources.o"
c++ -std=c++17 -g -O1 -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined \
  -DSK_HW_RESOURCES=1 -DSOLAR_OS_BUS_PORTABLE=1 -DSK_PRIMARY_RA8875=1 -DSK_PRIMARY_CS=37 -DSK_PRIMARY_RESET=9 \
  -DSK_AUDIO_SGTL5000=1 -DSK_UART_CONSOLE=1 -DSK_AMPLIFIER_SHUTDOWN_PIN=40 \
  -Itests/ports/hardware_stubs -Itests/host -Iinclude -Isrc -Isrc/services -Isrc/shell \
  -Isrc/platform/imxrt1062/teensy41 tests/ports/teensy41_python_hardware_test.cpp \
  src/platform/imxrt1062/teensy41/python_hardware_io.cpp src/platform/imxrt1062/teensy41/buses.cpp \
  src/platform/imxrt1062/teensy41/hardware.cpp "$python_hardware_test_dir/resources.o" -o "$python_hardware_test_dir/test"
"$python_hardware_test_dir/test"
python3 tests/ports/test_teensy41_python_machine.py
