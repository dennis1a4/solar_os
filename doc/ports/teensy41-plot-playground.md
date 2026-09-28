# Teensy Plot and Playground

The `teensy41_display` profile includes the shared Plot application and Playground
catalog browser. Both use the existing app registry and lifecycle. The original
USB-only profiles remain separate.

## Plot

Run on the LCD with the USB host keyboard:

```text
plot uptime --rate 250
plot -f /sd/readings.csv
plot -f /sd/readings.csv temperature humidity
```

`uptime` is a real scalar stream in seconds, provided as a simple live test.
Additional sensors require registered stream drivers. CSV files use the shared
Plot parser and controls: arrows pan/select series, +/- zoom, Space pauses a live
plot, A/R resets the view and scaling, Q/Esc/Ctrl-] exits.
See `less man:app.plot` for the upstream manual.

The local console owns the RA8875 graphics target; the USB console rejects
Plot without disturbing the LCD. Graphics use the controller's drawing engine,
not a software framebuffer. Text output continues to update the saved terminal
model while graphics are active, and exiting redraws the terminal. `lcd` reports
graphics ownership, presented frame count and last render time. `lcd dump`
continues to show terminal text, not graph pixels. The renderer currently covers
the primitives used by Plot, with the RA8875's fixed 8x16 font. It is not a
complete implementation of the shared graphics API.

## Playground

Start Ethernet before refreshing or installing:

```text
network up
network status
playground refresh
```

Refresh opens the browser. Wait for completion; use arrows/Enter to navigate,
I to install, R in an application's details to run, U to uninstall, and Q to
exit. Opening `playground` without `refresh` loads the saved catalog offline.
The browser works on either console, one instance at a time.

A small installation/run check after refreshing:

```text
playground search hello
playground install hello-python
playground run hello-python
hello-python
```

The catalog and installed files default to SD when mounted, otherwise flash.
`playground storage` reports the selection; `playground storage sd` or `flash`
changes it. Source URLs and preferences persist in flash. Existing settings
snapshots are read without rewriting them; the next changed setting is committed
in the extended format supporting 319-byte strings. Older firmware cannot read
new-format namespaces, so retain the prior firmware/settings backup if rolling
back.

Downloads use a bounded GET-only HTTP/HTTPS adapter over the existing Ethernet
transport. TLS verifies hostname, chain and certificate dates using the Teensy
RTC and a small embedded root set (DigiCert Global Root G2, USERTrust RSA/ECC,
ISRG Root X1, Sectigo Public Server Authentication R46/E46). Check `rtc` before using HTTPS. If it is wrong, `rtc set <UTC Unix seconds>`
sets it (obtain the value with `date -u +%s` on the host). The CLI loader in this
setup did not set the correct date automatically. A VBAT battery preserves the
clock across power loss. Keep this clock in UTC; timezone display can be added
separately. The planned coin-cell installation/retention is not yet tested. Certificate date checks are never disabled. Other HTTPS
sources may require adding their CA root. Redirects cannot downgrade HTTPS to
HTTP. Package size, SHA-256, manifest, archive paths, and staged installation
checks remain in the upstream Playground service.

Python scripts can use the port's current MicroPython APIs. Lua is not included.
Python graphics bindings are not implemented, so catalog entries requiring
`gfx` are marked unavailable even though native Plot can draw. Other upstream
Python extension APIs are also not automatically supplied by this port; a
catalog entry with incomplete requirements may still fail at runtime. Network
apps requiring `wifi` remain unavailable on this Ethernet-only board. Installing
Playground does not imply that every community application is ported.

## Memory policy

The board has 1 MiB internal RAM and 8 MiB fitted PSRAM. The SolarOS allocator
already prefers PSRAM for external-preferred/transient application allocations;
Python's heap, LCD text model, Plot samples, catalog, JSON and TLS buffers use it.
PSRAM is memory mapped and accessed with ordinary pointers, but cache misses
must cross the serial external-memory interface. It is appropriate for these
large buffers, not a replacement for fast internal stacks and timing-critical
or DMA storage. Plain third-party `malloc` remains internal. Allocation failure,
DMA placement and cache coherency still need deliberate handling.

For this workload the measured native graph render was about 63–85 ms; this
is an application timing, not a PSRAM bandwidth benchmark. Memory use stayed
stable across repeated CSV open/close cycles after libc formatting warm-up.

The port now initializes shared external BSS explicitly: Arduino's `EXTMEM`
section is not automatically zeroed. This preserves the shared services' normal
zero-initialized-global semantics without changing the system's main heap.

## Validation

```sh
bash scripts/ports/test_teensy41_http_host.sh
bash scripts/ports/test_teensy41_settings_host.sh
bash scripts/ports/test_teensy41_children_host.sh
bash scripts/ports/test_teensy41_lcd_host.sh
python3 scripts/ports/test_teensy41_plot_playground.py --log /tmp/plot-playground.json
```

The HTTP host test uses the firmware's pinned mbedTLS sources after a display
build fetches dependencies. It exercises fragmented reads/writes, fixed-length,
chunked and close-delimited responses, redirects, malformed input, cancellation,
timeout and cleanup. It does not substitute for certificate checks on hardware.
The device test needs exclusive serial access and an idle local keyboard. It
removes its temporary CSV, but leaves the official catalog and Hello Python
installed for manual use. `--plot-only` skips network/download checks.

Hardware validation completed on the fitted 800x480 RA8875: live and CSV Plot,
USB responsiveness during plotting, repeated app cleanup, real HTTPS catalog
refresh, Hello Python installation and execution (including its shell alias),
and unavailable-runtime checks. The optimized catalog GET took about four
seconds. TLS uses assembly/NIST arithmetic optimizations while preserving chain,
hostname and date verification. `SK_HTTP_DIAGNOSTICS=1` enables development-only
USB TLS timing messages; the normal build keeps both consoles' output separate.
The host suite also checks all bundled roots, expired dates and wrong hostnames.
`--playground-only` skips Plot checks when diagnosing downloads.

Firmware: 1,015,088 bytes flash, 424,256 bytes RAM1 and 225,704 bytes RAM2.
The independent-console regression also passes. The user confirmed a readable live graph,
Space pause/resume and Q returning to the shell.

Final Files regression passed: SD/flash copy/move, recursive copy, ZIP, editor
and Python child return, and repeated app cleanup. The memory assertion accepts
increased internal free space while still rejecting losses; PSRAM must match.
Log: `/tmp/teensy-plot-files-regression-final.json`.
