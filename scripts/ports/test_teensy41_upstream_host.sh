#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
test_dir=$(mktemp -d /tmp/solaros-upstream-host.XXXXXX)
cc -std=c11 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc/services tests/ports/teensy41_stusb4500_test.c src/services/solar_os_stusb4500.c -o "$test_dir/pd"
"$test_dir/pd"
cc -std=c11 -g -fsanitize=address,undefined -Isrc/services -c src/services/solar_os_midi_codec.c -o "$test_dir/midi.o"
c++ -std=c++17 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc/services -Isrc/platform/imxrt1062/teensy41 tests/ports/teensy41_midi_record_test.cpp "$test_dir/midi.o" -o "$test_dir/midi"
"$test_dir/midi"
c++ -std=c++17 -g -pthread -fsanitize=address,undefined -DSK_WEBRADIO=1 -DSK_SYNTH=1 -Itests/ports/radio_stubs -Itests/host -Isrc -Isrc/services -Isrc/platform/imxrt1062/teensy41 tests/ports/teensy41_radio_audio_test.cpp src/platform/imxrt1062/teensy41/radio_audio.cpp -o "$test_dir/radio"
"$test_dir/radio"
