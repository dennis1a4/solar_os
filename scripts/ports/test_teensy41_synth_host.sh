#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
synth_test_dir=$(mktemp -d /tmp/solaros-synth-test.XXXXXX)
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -DSOLAR_OS_DSP_HOST_TEST -include tests/host/compat.h \
  -Itests/host -Isrc -Isrc/services \
  tests/ports/teensy41_synth_voice_test.c src/services/solar_os_synth_voice.c \
  src/services/solar_os_dsp.c -lm -o "$synth_test_dir/voice"
"$synth_test_dir/voice"
