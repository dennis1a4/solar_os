#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
ftp_test_dir=$(mktemp -d /tmp/solaros-ftp.XXXXXX)
flags=(-g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined)
c++ -std=c++17 "${flags[@]}" -DSK_FTP=1 \
  -Itests/ports/ftp_stubs -Isrc/platform/imxrt1062/teensy41 \
  src/platform/imxrt1062/teensy41/ftp_socket.cpp tests/ports/teensy41_ftp_socket_test.cpp \
  -o "$ftp_test_dir/socket-test"
"$ftp_test_dir/socket-test"
cc -std=c11 -D_DEFAULT_SOURCE -DSK_UPSTREAM_SHELL=1 "${flags[@]}" \
  -Wno-unused-function -ffunction-sections -fdata-sections -include tests/host/compat.h \
  -Itests/ports/ftp_protocol_stubs -Itests/host/rtspd_stubs -Itests/host \
  -Isrc -Isrc/jobs -Isrc/services -Iinclude \
  tests/ports/teensy41_ftp_protocol_test.c src/services/solar_os_ftp.c \
  src/jobs/solar_os_ftpd_job.c src/platform/imxrt1062/teensy41/shell_file_ops.c \
  -pthread -Wl,--gc-sections -o "$ftp_test_dir/protocol-test"
"$ftp_test_dir/protocol-test" "$ftp_test_dir"
python3 tests/test_ftp_feature.py

job_flags=("${flags[@]}" -DSOLAR_OS_HEADLESS=1 -DSK_FTP=1 -DSOLAR_OS_JOBS_MAX=5
  -ffunction-sections -fdata-sections -include tests/ports/jobs_stubs/compat.h
  -Itests/host -Isrc -Isrc/apps -Isrc/services -Isrc/shell)
for source in src/solar_os.c src/solar_os_jobs.c src/solar_os_scheduler.c src/shell/solar_os_shell_parse.c; do
  cc -std=c11 "${job_flags[@]}" -c "$source" -o "$ftp_test_dir/$(basename "$source").o"
done
c++ -std=c++17 "${job_flags[@]}" tests/ports/teensy41_jobs_test.cpp "$ftp_test_dir/"*.o \
  -Wl,--gc-sections -o "$ftp_test_dir/jobs-test"
"$ftp_test_dir/jobs-test"
