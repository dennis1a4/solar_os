#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
entropy_test_dir=$(mktemp -d /tmp/solaros-entropy-build.XXXXXX)
python3 - "$entropy_test_dir" <<'PY'
from pathlib import Path
import sys
s=Path('src/platform/imxrt1062/teensy41/network_socket.cpp').read_text()
a=s.index('    case SK_NET_ENTROPY: {')
b=s.index('\n#endif',a)
Path(sys.argv[1],'entropy_case.inc').write_text(s[a:b])
PY
c++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$entropy_test_dir" tests/ports/teensy41_entropy_test.cpp -o "$entropy_test_dir/test"
"$entropy_test_dir/test"
