#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -Isrc/services tests/ports/teensy41_telnet_codec_test.c src/services/solar_os_telnet_codec.c \
    -o /tmp/solaros-telnet-codec
/tmp/solaros-telnet-codec
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -Isrc/services -c src/services/solar_os_telnet_codec.c -o /tmp/solaros-telnet-codec.o
c++ -std=c++17 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror -DSK_TELNETD=1 \
    -Itests/ports/telnet_stubs -Itests/host -Isrc -Isrc/shell -Isrc/services \
    -Isrc/platform/imxrt1062/teensy41 tests/ports/teensy41_telnet_service_test.cpp \
    src/platform/imxrt1062/teensy41/telnet_console.cpp /tmp/solaros-telnet-codec.o \
    -o /tmp/solaros-telnet-service
/tmp/solaros-telnet-service
