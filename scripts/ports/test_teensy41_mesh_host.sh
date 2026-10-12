#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
cc -std=c99 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -Isrc/services -Isrc/services/mesh -Isrc/vendor/nanopb -Isrc/vendor/meshcore_companion \
    tests/ports/teensy41_mesh_test.c src/services/mesh/*.c \
    src/vendor/nanopb/*.c src/vendor/meshcore_companion/*.c -o /tmp/solaros-mesh-host
/tmp/solaros-mesh-host
cc -std=c99 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
    -DSK_MESH_CLIENT=1 -Itests/host -Isrc -Isrc/apps -Isrc/services -Isrc/shell \
    tests/ports/teensy41_mesh_view_test.c src/apps/solar_os_mesh_view.c -o /tmp/solaros-mesh-view-host
/tmp/solaros-mesh-view-host
