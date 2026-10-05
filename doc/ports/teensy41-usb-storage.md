# Teensy 4.1 USB flash drives

The display profile enables `SK_USB_STORAGE`, using the existing USBHost_t36
host controller and hubs. One USB mass-storage device and its first supported
partition are exposed as `/usb`. A keyboard may share the host hub. This is the
Teensy's host connector, separate from its computer-facing USB serial port.

```
usb                     # connection, filesystem, capacity, open handles
ls /                    # sd, flash, usb (only mounted volumes)
ls /usb
cd /usb
usb eject               # close files/apps first; flush before unplugging
usb mount               # retry/remount without unplugging
df                      # maintained FAT usage after the first scan
df --refresh            # recount USB usage; close USB files/apps first
```

Files discovers the mount through the shared mount enumeration API. Shell file
commands and MicroPython use the same storage adapter. File copies between
USB, SD and flash are supported; cross-volume moves copy then remove the source.
No USB formatting command is provided. Existing drives are never reformatted.

## Formats and limits

- Recommended initial setup: a 16 or 32 GB drive, one FAT32 MBR partition.
  Linux calls FAT16/FAT32 with long filenames `vfat`.
- Compiled library support: FAT16, FAT32 and exFAT. NTFS, ext4, APFS and encrypted
  volumes are not supported. exFAT and other capacities still need device tests.
- 512-byte logical sectors only. Other sector sizes are refused before reading
  partition data into the library's 512-byte buffers.
- USBHost_t36 uses 32-bit sector addresses and READ CAPACITY(10). Treat drives
  of 2 TiB or greater as unsupported; a maximum hardware capacity is not yet
  validated. There is no 32 GB drive-size cap in this integration.
- SolarOS's current POSIX adapter has signed 32-bit file offsets: individual
  files are limited to 2,147,483,647 bytes, including append writes. exFAT does
  not remove this application-layer limit. Oversized stat/open/seek operations
  return an error rather than wrapping offsets.
- Paths are at most 159 bytes plus terminator. There are 16 shared POSIX file
  descriptors across SD, flash and USB. Only one USB volume is exposed.
- Partition discovery is delegated to USBHost_t36 (MBR, extended and supported
  GPT basic-data partitions); only the first supported partition is exposed.
  MBR is the compatibility baseline. GPT and unpartitioned media are untested.

## USB transport and cached RTOS stacks

The LCD console stack is in cached OCRAM, unlike a normal Arduino sketch's
DTCM stack. The bundled MSC library uses stack-resident command/status DMA
buffers without cache maintenance. The first integration stalled during drive
initialization. `scripts/platformio_teensy_usb_storage.py` adapts a generated
build-directory copy of the pinned MSC source: command/status wrappers use
serialized static DTCM storage, blocking command/status waits have a three-second
timeout, and failed initialization is not retried internally. The installed SDK
is untouched; exact patch counts catch unexpected upstream changes.

Sector I/O uses a 4 KiB, 32-byte-aligned OCRAM bounce buffer, including
directory/partition reads and data supplied by either console or PSRAM. The
base USB driver performs cache maintenance for these data transfers. Reads use
at most eight sectors per synchronous USB request; FAT scan callbacks run in
task context after each completed batch. Writes remain single-sector. The buffer
replaces the former 512-byte DTCM buffer: net static RAM growth is 3.5 KiB, with
no PSRAM allocation. A timed-out transport is
quarantined until unplugged; late completions cannot reuse a new request's
buffers. `test_teensy41_usb_driver_host.py` checks the adaptation and verifies
actual linked DMA buffer addresses are in DTCM (10 symbols checked).

The tested setup uses a powered USB hub, with the keyboard and 16 GB drive
connected together. We have not measured power draw or retested the unpowered
hub after the driver correction; the earlier freeze is not proof of overcurrent.

## Removal and concurrency

Storage calls and host filesystem discovery share one recursive mutex. USB
interrupt callbacks invalidate the media gate without destroying the SdFat
volume. Stale files return ENODEV; the block-device gate prevents later close
operations from flushing old caches to newly inserted media. Remount waits
until stale file/directory handles have been closed. `usb eject` refuses while
handles are open, flushes the root/volume cache, and suppresses automatic
remounting until removal or `usb mount`.

Unexpected removal during a write can still lose data; use `usb eject` first.
The driver uses synchronous USB transfers, so a slow/faulty drive can delay
console work. USB storage is a new hardware path, not a guarantee that every
USB bridge or flash-drive controller is compatible.

## Usage accounting and scan timing

SdFat already caches FAT free-cluster counts and maintains them on allocation
and release. `df` reuses that counter. `df --refresh` reloads USB volume
accounting under the storage mutex and performs a fresh count; it refuses while
any USB file or directory is open. Root metadata is synced before reload.
Other volume rows display normally; without mounted USB, refresh is a no-op.
A reconnect or logical remount starts a fresh count. exFAT uses a bitmap scan;
this change does not add an exFAT usage cache.

On 2026-10-05, the powered-hub 16 GB FAT32 drive's cold USB scan fell from
45.828 to 5.791 seconds with SD already warmed (about 7.9 times faster).
Repeated `df` takes 0.031 seconds; explicit USB refresh takes about 5.82 seconds.
The first whole-system `df` after firmware boot took 11.298 seconds, including
initial SD accounting. Results depend on drive size, format and controller.

The installed `teensy41_telnet_legacy` build passed the batch helper's ASan/UBSan
tests and `test_teensy41_df.py`: cached counts match recounts after create,
truncate, rename and delete; logical remount preserves usage; live handles
refuse refresh; a 513 KiB file hashes correctly through 16 KiB Python reads.
Generated fixtures were removed and the original USB allocation restored.
Evidence: `/tmp/teensy-df-device.json`. Physical removal during active I/O and
other media remain separate tests.

## Earlier validation

Both `teensy41_display` and the non-USB `teensy41_network` build passed.
Corrected display firmware uploaded successfully. The user confirmed keyboard
operation with the drive inserted. Runtime reports FAT32, 15,260 MiB at `/usb`.

Passing logs:
- `/tmp/teensy-usb-empty.json`: no-drive status and mount listing.
- `/tmp/teensy-usb-flash-regression.json`: SD/flash copy/move, file modes,
  editor, Python, descriptor cleanup, memory and root protection.
- `/tmp/teensy-usb-test-final.json`: USB read/write, long names, append/seek,
  native copy/rename/delete, cross-volume moves, three eject/remount cycles,
  data persistence, binary reading on the LCD console, and busy-handle eject
  refusal. Fixture folder: `_usb_test_40d5921d` on USB, SD and flash.
- Driver adaptation and linked DTCM buffer check passed; all 14 manual-generator
  tests passed. Final observed internal heap: 32,660 free / 81,120 bytes;
  PSRAM: 8,202,576 free / 8,388,608 bytes.

Files root listing also shows `/usb` in both panes (`/tmp/teensy-usb-files-listing.txt`).
Subsequent single-cycle physical safe removal and read-only surprise-removal/
stale-handle recovery passed on this USB baseline; see
[observed reconnect results](teensy41-hotplug-tests.md). Repeated cycles, other
media, active-I/O removal and regression on newer firmware remain in the
[master checklist](teensy41-test-checklist.md). The temporary read-only handle
was closed at the end of testing.

```
python3 scripts/ports/test_teensy41_usb_storage.py --log /tmp/teensy-usb-test.json
```

The script only creates unique `_usb_test_*` folders. It checks root mount
listing, binary read/write, append/update/seek, long names, directory listing,
SD/flash isolation, cross-volume copies/moves, repeated eject/remount,
persistence, and refusal to eject a handle held by the other console. Successful
runs remove their generated fixtures; `--keep-fixtures` retains them for inspection.
Failed runs retain evidence. Physical surprise-removal and other formats/capacities
require separate checks. `--no-drive` checks the empty-host case.

Upstream reference: [PJRC USB disk support](https://www.pjrc.com/2022/08/).
The exact format and addressing limits above were also checked against the
installed USBHost_t36 and SdFat sources used by this build.
