# Teensy quick reference: View and help

Checked against the Teensy port source on 2026-10-03. Features depend on the
firmware profile. This guide describes the current workstation/LCD build;
it does not imply that upstream ESP-only features are available.

## View: image files

| Format | Teensy support |
| --- | --- |
| PNG (`.png`) | Yes; device acceptance tested |
| JPEG (`.jpg`, `.jpeg`) | Yes; device acceptance tested |
| GIF (`.gif`) | Decoder integrated, including animation; bounded frame/canvas memory |
| BMP (`.bmp`) | Uncompressed 1/4/8/24/32-bit images; no 16-bit or compressed BMP |
| PBM/PGM/PPM (`.pbm`, `.pgm`, `.ppm`, `.pnm`) | PNM P1–P6, ASCII and binary variants |
| WebP | Not enabled on Teensy; its decoder adapter returns unsupported |
| SVG, TIFF, PDF | Not supported by View |

GIF/BMP/PNM have code paths in this build, but do not have the same recorded
hardware acceptance coverage as PNG/JPEG. View identifies formats from the file
contents, not just the filename extension.

Run from the LCD shell:

```text
view /sd/photo.png
view -fit /sd/photo.jpg
view -actual /sd/photo.jpg
```

| Key | Action |
| --- | --- |
| `F` | Toggle fit/actual size |
| `0` | Select actual size and reset pan |
| `1` | Select fit to screen |
| Arrow keys | Pan |
| `Esc` or `Ctrl+]` | Exit to the calling shell/app |

Fit is the default. A text-only USB/Telnet console cannot claim the LCD.
PNG/JPEG decoding, GIF frames and other apps compete for PSRAM. The normal
image pixel bound is 2 × 1024 × 1024; GIF has additional canvas/frame bounds.
Being below the limit or selecting fit does not guarantee an image will fit in
available memory. Fit-mode JPEG can decode to a display-sized destination.

For text/Markdown use `less /sd/notes.md` or `edit /sd/notes.md`; for binary
inspection use `hexedit /sd/file.bin`. View is an image viewer, not a PDF reader.

## Finding help today

```text
help                 # Browse the offline manual tree
help view            # Start the browser on the View topic
man view             # Read View's manual page
man edit             # Read editor usage and controls
man -k image         # Search manual metadata
man --list           # List embedded topics
apps                 # List apps available in this firmware
commands             # List shell commands
help status          # Inspect the manual's current source/status
```

The comments above explain the commands; type only the command itself.
The help browser uses arrows and Enter; Q exits. Manual text is available on
LCD and text shells. Browsing the instructions for a graphical app does not
require permission to launch that graphical app on the current console.

The current Teensy documentation selection contains **83 topics**, including
pages for **all 24 apps in its documentation allowlist**. Build gates can reduce
the number actually embedded. These are generated app pages, not necessarily
24 separate Markdown files. Having a page does not prove every statement in an
inherited upstream page matches the port; those pages still need a systematic
capability/controls audit.

Manual corrections in this change require a firmware rebuild to appear on the
device. No new image was built or flashed. The installed View page may still
mention WebP. Signed `help update` downloads are not integrated on Teensy.

## Recommended consistent help interface — proposed, not implemented here

Extend the existing manual system instead of creating another documentation
store. Canonical upstream topics live in `doc/manual/*.md`; generated command
and app topics feed the firmware registry, browser, pager and website. The
Teensy selector in `scripts/ports/teensy41_manual.py` filters unavailable features
and overrides narrower port behavior.

| Entry point | Consistent behavior to implement |
| --- | --- |
| `help` | Existing searchable/grouped topic browser |
| `help TOPIC`, `man TOPIC` | Same topic and port-accurate content |
| `APP --help`, `COMMAND --help` | Compact usage/options/examples, without launching the app or performing the command |
| In-app Help action, preferably `F1` | Contextual controls and relevant topic; close help to resume exactly where the user was |
| `man -k QUERY` | Existing search, extended with meaningful task/format keywords |
| Error/usage messages | Short explanation and `See: man TOPIC` |
| Persistent app footer | Visible hint for Help, Back/Exit and the most relevant controls |

All entry points should draw from one topic record: purpose, syntax, arguments,
controls, examples, supported formats/capabilities, limits and related topics.
Do not maintain separate hand-written strings for shell help, in-app help and
printed quick references. Eventually move substantial Teensy overrides into
structured documentation data so port text is also easy to review.

Implement `--help` in the common command/app dispatch path, before argument
validation or resource acquisition. Only intercept the command's help option,
not a script's arguments or text following an explicit `--` delimiter. Help
must not initialize hardware, open a UART, start a worker or launch a VM.

Implement in-app help through shared session/child-app infrastructure, with
text/graphical presentation selected by the owning console. Preserve the
parent's buffers, selection, scroll position, graphics and resource ownership.
Close only the help child on Back/Exit; repeated help must not consume retained
session slots or leak memory. Use a bounded text fallback if a richer view
cannot be admitted.

Audit existing function-key assignments before making F1 universal. Raw COM,
SSH and language REPL contexts must retain their normal key delivery; give
those contexts an explicit local Help action through their escape/menu path.
Do not steal keys intended for a remote computer or user program. Provide a
non-function-key route through shell help for terminals/keyboards lacking F1.

Keep the manual offline and build-aware. Do not assume an SD card, network,
mouse or touch panel. Use a pager and width-aware layout on both LCD and terminal
connections. Shared metadata should also drive completion and coverage checks.

Suggested implementation order:

1. Audit the existing pages against registered apps/commands and port behavior;
   correct capabilities and examples. Add coverage checks against actual build
   registrations, beyond the documentation allowlist check added here.
2. Add central, side-effect-free `--help` and consistent topic hints.
3. Add reusable context help with state-preserving return; integrate each app,
   handling raw-terminal/function-key conflicts explicitly.
4. Generate user quick references from the same topic data and test LCD/USB/
   Telnet behavior, nested help, cancellation and repeated memory cleanup.

This change adds the quick reference, corrects View's Teensy manual selection
and the old graphics-guide key mapping, and tests documentation coverage. It
does not implement the proposed universal help key or dispatcher behavior.
