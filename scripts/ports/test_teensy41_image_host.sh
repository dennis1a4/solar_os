#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
image_test_dir=/tmp/solaros-image-host
mkdir -p "$image_test_dir"
python3 - <<'PY'
from PIL import Image, ImageDraw
im=Image.new('RGB',(320,192));d=ImageDraw.Draw(im)
for i,c in enumerate(['red','green','blue','yellow','cyan','magenta','white','black']):d.rectangle((i*40,0,i*40+39,191),fill=c)
for ext in ['png','jpg']:im.save('/tmp/solaros-image-host/colors.'+ext)
PY
cc -std=c11 -O1 -g -fsanitize=address,undefined -Itests/host -Isrc/platform/imxrt1062/teensy41/compat -Icomponents/stb_image/include \
    components/stb_image/stb_image_port.c components/stb_image/jpeg_fast.c tests/ports/teensy41_image_test.c -lm -o "$image_test_dir/test"
"$image_test_dir/test" "$image_test_dir/colors.png" "$image_test_dir/colors.jpg"
