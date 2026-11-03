#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
doc_test_dir=$(mktemp -d /tmp/solaros-documents.XXXXXX)
trap 'rm -rf "$doc_test_dir"' EXIT
flags=(-std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
cc "${flags[@]}" -DSOLAR_OS_WRITER_HOST_TEST -Itests/host -Isrc/apps tests/host/writer_buffer_test.c src/apps/solar_os_writer_buffer.c -o "$doc_test_dir/buffer"
"$doc_test_dir/buffer"
cc "${flags[@]}" -Itests/host -Isrc/services -Isrc/apps tests/host/writer_files_test.c src/apps/solar_os_writer_files.c -o "$doc_test_dir/files"
"$doc_test_dir/files"
cc "${flags[@]}" -Wno-unused-function -Wno-unused-variable -D_GNU_SOURCE -ffunction-sections -fdata-sections -include tests/host/compat.h -Itests/host -Isrc -Isrc/services tests/host/doc_layout_test.c tests/host/doc_test_support.c src/services/solar_os_doc.c -Wl,--gc-sections -o "$doc_test_dir/layout"
"$doc_test_dir/layout"
python3 scripts/ports/teensy41_document_fixtures.py "$doc_test_dir"
miniz=.pio/teensy-files-deps/miniz
cc "${flags[@]}" -Wno-unused-function -Wno-unused-variable -D_GNU_SOURCE -ffunction-sections -fdata-sections -include tests/host/compat.h -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ZLIB_APIS -DMINIZ_NO_MALLOC -Itests/host -Isrc -Isrc/services -I"$miniz" -I.pio/build/teensy41_power_board/miniz-config tests/ports/teensy41_epub_test.c tests/host/doc_test_support.c src/services/solar_os_{doc,epub,html,xml,zip}.c "$miniz"/miniz.c "$miniz"/miniz_tinfl.c -Wl,--gc-sections -o "$doc_test_dir/epub"
"$doc_test_dir/epub" "$doc_test_dir/book.epub"
cc "${flags[@]}" -D_GNU_SOURCE -DSK_UPSTREAM_SHELL=1 -include tests/host/compat.h -Itests/host -Isrc/services tests/ports/teensy41_paths_test.c src/platform/imxrt1062/teensy41/shell_paths.c -o "$doc_test_dir/paths"
"$doc_test_dir/paths"
