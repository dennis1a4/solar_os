# Teensy RA8875 graphics and image viewing

**Python graphics and View validated on 2026-09-28.** Python and View are integrated.
Tile-content comparison now avoids retransmitting unchanged tiles after apps
clear/redraw their canvas. The hardware acceptance suite, repeated lifecycle checks and official
Playground Mandelbrot rendering have passed. See the
[handoff](teensy41-handoff.md) for the latest validation record.

The `teensy41_display` profile includes the shared SolarOS software renderer,
View, native Invaders, and `solaros.gfx` for Python applications. The LCD/USB-host
keyboard session owns the display. USB serial stays usable but cannot claim the
LCD, including through Python's explicit `gfx.begin("display0")` argument.

## Viewing images

```text
view /sd/photo.png
view /sd/photo.jpg
view -actual /sd/photo.jpg
```

View fits the image to the screen by default. `1` selects actual size, arrows
pan, `0` resets the pan, and `F` returns to fit. Escape or Ctrl-] returns to the
shell/parent app. The existing Files image association opens View with Enter.

PNG, JPEG, GIF and the upstream built-in BMP/PNM paths use the shared decoders.
PNG/JPEG are the acceptance-test targets. WebP is deliberately unavailable in
this profile. The indexed canvas uses upstream's 216-color RGB cube plus gray
and theme entries, converted to RGB565 for the panel. This is not a full
24-bit photographic framebuffer. Transparent pixels use the upstream decoder's
compositing behavior. Very large images can exceed the 8 MiB PSRAM budget;
View reports an allocation/size error. Upstream's image limit is 2 megapixels,
not a promise that every image under that limit fits alongside all other apps.

## Drawing and graphical apps

```text
invaders
playground install mandelbrot-python
playground run mandelbrot-python
```

Run graphical apps from the LCD shell. Playground now accepts the `gfx`
requirement for Python. Lua remains unavailable; apps requiring other missing
services still need their respective ports. Enabling graphics does not add
Python audio, contacts, touch, networking APIs beyond those already provided,
or every other upstream module.

The Python adapter supplies begin/end, colors, gray/RGB, all upstream fonts,
pixel/line/rectangle/circle primitives, UTF-8 text, Open Iconic symbols,
bitmap/sprite, present/refresh and keyboard getch. See the upstream
[Python graphics API](../manual/python.gfx.md). `solaros.should_exit()`,
`solaros.sleep_ms()` and `solaros.time.sleep_ms()/ticks_ms()` support cooperative
loops. Named auxiliary displays are not implemented; `display0` is the LCD.
Closing Python, cancellation and script errors release the graphics canvas.

## Implementation and memory

The former small native RA8875 Plot renderer is replaced by the shared
`solar_os_gfx.c` renderer. This preserves upstream text, icons, primitive,
bitmap, snapshot and indexed-color semantics. A 48,000-byte font mask is
allocated once in PSRAM; the roughly 384 KiB indexed canvas and dirty map are
allocated on entry and released on exit. Image decoding also uses PSRAM.

`graphics.cpp` adapts that canvas to the panel. It converts dirty horizontal
8-pixel tile runs into RGB565 scanlines and writes them through the RA8875
library. It compares dirty 8x8 tile hashes against the shared canvas's 6,000
presented-hash slots. Palette changes and graphics re-entry force repainting;
zero hashes (including snapshot invalidation) force the affected tiles to be
sent. Hashes are committed after all eight scanlines transfer successfully.
The adapter validates data, palette, dirty-map and hash-array bounds and accepts
only the current 800x480, unrotated INDEX8 surface. Other orientations require
an adapter extension. No full RGB565 copy is needed. Safe presentation boundaries yield the
console gate periodically so USB can run. Native decode/render computation
outside those boundaries can still briefly delay the other console.

The shared u8g2 library, fonts and image decoder are vendored in this repository.
These objects and the renderer execute
from cached flash to preserve internal RAM. Unrelated USB-only profiles remain
separate.

The Linux upload path builds a pinned PJRC Teensy Loader CLI 2.3 in
`.pio/teensy-tools/loader`; the bundled 2.2 loader misparses HEX addresses above
`0x60100000`. The first upload needs network access, `make`, a C compiler and
libusb-compat development headers. The source revision is recorded in
`scripts/ports/upload_teensy41.py`. The loader explicitly selects `TEENSY41`.
Other host platforms must provide a compatible current loader.

## Performance

On the 2026-09-28 firmware, a Python clear/draw/present benchmark measured
1,739 ms for the initial full frame, 33 ms for an identical redraw, and 36 ms
for moving an 8x8 rectangle. A sampled Invaders update took 33 ms. Actual frame
rate also includes application rendering and scheduling. Full-screen changes
still take about 1.73 seconds at the existing 4 MHz SPI setting; this is not a
video-rate full-screen renderer. SPI speed and temporary wiring are unchanged.

## Verification

```sh
bash scripts/ports/test_teensy41_graphics_host.sh
bash scripts/ports/test_teensy41_image_host.sh
python3 scripts/ports/test_teensy41_graphics.py --mandelbrot --log /tmp/teensy-graphics-complete.json
```

The host test needs Pillow and a C compiler with ASAN/UBSAN. It checks decoded
PNG/JPEG colors/dimensions, repeated cleanup, pixel limits and truncated files.
The hardware test needs Pillow, pyserial, exclusive USB serial access and an
idle local keyboard. It creates a unique `/sd/graphics-demo-*` folder, retains
its images and Python example, and leaves a PNG displayed for visual checking.

The presenter host test uses ASAN/UBSAN and compares randomized dirty updates
against a full-frame reference. It covers palette changes, padded strides,
invalid bounds/rotation, reset, zero-hash invalidation, uncached presentation,
and retry after a partial transport failure. The expanded hardware test measures
Python presentation times and checks repeated Python/native cleanup and
Files-to-View return. `--mandelbrot` additionally installs/runs the official
Playground example and requires Ethernet access.

2026-09-28 device evidence: `teensy-graphics-final.json` passes the expanded
core suite, including five additional Python/Invaders cycles with exactly stable
internal/PSRAM free memory. `teensy-graphics-mandelbrot.json` passes the official
package installation, all 480 rendered rows, USB responsiveness and Q cleanup.
The Mandelbrot calculation itself takes several minutes at 800x480. Logs and
firmware are retained in `../solar_os-baselines/2026-09-28-graphics/`.

The user also confirmed readable demo text, the folder icon and clean shapes,
and working physical Left/Right movement and Space firing in Invaders. This
completes validation for the current display setup and supported feature scope.
