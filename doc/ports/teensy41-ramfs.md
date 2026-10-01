# PSRAM temporary filesystems (original workstation item 7)

The Teensy port reuses the shared SolarOS RAMFS allocator, file tree and file
operations through a portable backend interface. The Teensy storage router
adapts descriptors and libc streams alongside SD, USB and QSPI flash.

## Using RAMFS

```text
ramfs mount /ram 1m
ramfs
mkdir /ram/work
python -c "f=open('/ram/work/hello.txt','w');f.write('Hello from RAM\n');f.close()"
cat /ram/work/hello.txt
files /ram
cp /ram/work/hello.txt /sd/hello.txt
ramfs unmount /ram
```

RAMFS is explicitly mounted; no PSRAM is reserved for a filesystem at boot.
Mounts and all their contents disappear on unmount or reboot. Copy anything
needed later to SD, USB or flash before unmounting. Mounts are global across
consoles and survive console disconnect, unlike console-owned GPIO claims.

`ramfs [status]` lists mounts, usable quota, used/free bytes, files, directories
and open file/directory handles. `df` includes mounted RAM filesystems. Ordinary
paths work with shell file commands, Files/Edit, Python file I/O, scripts and
archives. Directory and path completion uses on-demand enumeration. `ramfs`
subcommands, active mount names and common quota sizes also complete with Tab.

## Limits and lifetime

- Up to four mounts, each requesting 1 KiB–4 MiB. Sizes accept decimal bytes or
  `k`/`m` suffixes (1024-based). Mount names are one top-level component, with
  letters, digits, dash or underscore, maximum 14 characters after `/`.
- `/`, `/sd`, `/usb` and `/flash` are reserved. Nested mounts are unsupported.
  Unknown mount names fail instead of falling through to SD. Use explicit
  `/sd/...` paths for SD files; the old implicit bare-root SD alias is disabled
  in this profile. Relative paths under a mounted current directory work normally.
- Mount allocation uses `SOLAR_OS_MEMORY_EXTERNAL_REQUIRED`; it never falls back
  to internal RAM. The command checks that 512 KiB of PSRAM will remain for apps
  before allocating. Allocation can still fail due to fragmentation/concurrent
  app allocations. There is no new worker task, persistent mount configuration,
  swapping, permissions enforcement or filesystem index.
- The arena includes file contents, nodes, names, directory iterators and block
  metadata. Reported usable total excludes the initial block header; free space
  can be fragmented. Filling the quota returns ENOSPC and preserves prior data.
  File expansion can need temporary space if an allocation cannot grow in place.
- The Teensy router shares 16 file descriptors across all volumes. RAMFS also
  limits each mount to 16 files open at once. File streams add their normal
  libc overhead outside the arena; directory wrappers use the existing router.
- Any open file or directory handle blocks unmount. This includes a retained or
  background Python worker's files. Close them or stop the worker first.
  Shell working-directory strings and editor buffers are not open handles: they
  do not pin mounts. Return to `/` if another console unmounts your directory.
- Removing/renaming entries while a directory iterator is open returns EBUSY.
  This conservative rule prevents iterator pointers becoming invalid between
  calls. Open files cannot be unlinked; directory moves into descendants fail.
  Renames do not replace existing destinations. Cross-volume `mv` copies a
  regular file and removes its source after the destination is closed/synced.
- All Teensy routing, lifecycle commands and completion operations take the
  shared recursive storage lock. Per-mount mutexes protect the shared backend.
  Portable API users must follow the same outer-lock contract, especially when
  resolving a mount and opening a handle. Unmount cannot race a routed operation.

## Validation

Host ASan/UBSan covers quota exhaustion, prior-data integrity, sparse seek,
append, directory enumeration, open file/directory unmount rejection, invalid
mount names, rename cycles, descriptor exhaustion without truncation, and
20 mount/unmount cycles with exact allocation recovery. Eight manual tests pass.

Device acceptance passed: `/tmp/teensy-ramfs-device.json` and supplemental
`/tmp/teensy-ramfs-edges.json`. Five cycles recovered exactly 28,516 internal /
8,158,704 PSRAM bytes free. Editor create/overwrite saves, shared LCD access,
four mounts, flash transfers and actual reboot volatility passed. The first
reboot test opened a stale USB handle; retrying after enumeration fixed the host
test, with no firmware change. Long-running mixed-workload logging remains a
follow-up test. The script uses
unique temporary mount and SD names, removes its SD fixture and closes/unmounts
its own resources. It exercises Python, Files, archives, cross-volume copy/move,
completion, `df`, retained/background workers and repeated memory recovery.
See the handover for the installed image, final acceptance and checkpoint.

Run from the repository root:

```sh
bash scripts/ports/test_teensy41_ramfs_host.sh
python3 scripts/ports/test_teensy41_ramfs.py --log /tmp/teensy-ramfs-device.json
```
