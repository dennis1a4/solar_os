#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
audio_test_dir=$(mktemp -d /tmp/solaros-audio.XXXXXX)
# Generated quiet test tones; no copyrighted music or personal files.
ffmpeg -hide_banner -loglevel error -f lavfi -i 'sine=frequency=440:duration=1' -af volume=0.2 -ar 44100 -ac 2 -codec:a libmp3lame -b:a 128k "$audio_test_dir/stereo.mp3"
ffmpeg -hide_banner -loglevel error -f lavfi -i 'sine=frequency=660:duration=1' -af volume=0.2 -ar 48000 -ac 1 -codec:a libmp3lame -b:a 96k "$audio_test_dir/mono48.mp3"
ffmpeg -hide_banner -loglevel error -f lavfi -i 'sine=frequency=880:duration=1' -af volume=0.2 -ar 22050 -ac 1 -codec:a pcm_s16le "$audio_test_dir/mono22.wav"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -DSK_AUDIO_PLAYER=1 -DSOLAR_OS_PACKAGE_SERVICE_AUDIO_CODECS=1 \
  -DSOLAR_OS_AUDIO_CODEC_HOST_TEST -DMINIMP3_NO_SIMD -DMINIMP3_ONLY_MP3 \
  -Itests/host -Isrc -Isrc/services -Isrc/platform/imxrt1062/teensy41 \
  -Icomponents/minimp3/include tests/ports/teensy41_audio_files_test.c \
  src/platform/imxrt1062/teensy41/audio_files.c src/services/solar_os_audio_codec.c \
  src/services/solar_os_audio_pcm.c components/minimp3/minimp3_impl.c \
  -o "$audio_test_dir/files"
"$audio_test_dir/files" "$audio_test_dir/stereo.mp3" "$audio_test_dir/mono48.mp3" "$audio_test_dir/mono22.wav"
echo "Generated audio fixtures retained in $audio_test_dir"
