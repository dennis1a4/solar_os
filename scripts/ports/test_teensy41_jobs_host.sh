#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
jobs_test_dir=$(mktemp -d /tmp/solaros-jobs.XXXXXX)
flags=(-O1 -g -Wall -Wextra -Werror -DSOLAR_OS_HEADLESS=1 -ffunction-sections -fdata-sections -fsanitize=address,undefined -include tests/ports/jobs_stubs/compat.h -Itests/host -Isrc -Isrc/apps -Isrc/services -Isrc/shell)
for source in src/solar_os.c src/solar_os_jobs.c src/solar_os_scheduler.c src/shell/solar_os_shell_parse.c; do
  cc -std=c11 "${flags[@]}" -c "$source" -o "$jobs_test_dir/$(basename "$source").o"
done
c++ -std=c++17 "${flags[@]}" tests/ports/teensy41_jobs_test.cpp "$jobs_test_dir/"*.o -Wl,--gc-sections -o "$jobs_test_dir/jobs"
"$jobs_test_dir/jobs"
cc -std=c11 -D_DEFAULT_SOURCE "${flags[@]}" -DSK_BACKGROUND_JOBS=1 \
  tests/ports/teensy41_schedule_time_test.c src/platform/imxrt1062/teensy41/schedule_services.c \
  -Wl,--gc-sections -o "$jobs_test_dir/time"
"$jobs_test_dir/time"
for queued in 0 1; do
  cc -std=c11 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE \
    -DSOLAR_OS_SCHEDULE_RUNNER_QUEUES="$queued" -include tests/host/compat.h \
    -Itests/host/schedule_stubs -Itests/host -Isrc -Isrc/services \
    tests/host/schedule_test.c src/services/solar_os_schedule.c -o "$jobs_test_dir/schedule"
  "$jobs_test_dir/schedule"
done
