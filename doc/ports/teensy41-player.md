# Teensy folder music player

The integrated Teensy display/legacy profiles now build solarOS's shared
`solar_os_player.c`, with a Teensy folder provider and file-decoder worker
adapter. The ESP player retains its saved-playlist, graphical and seek behavior.
The Teensy version uses the terminal interface on LCD, USB or Telnet; no seeking
or saved playlist is exposed.

## Commands and controls

```
player /sd/music
player --shuffle /sd/music
player --shuffle --repeat all /sd/music
```

Omit the folder to use the current directory. Only visible regular `.mp3` and
`.wav` files directly in that folder are included, case-insensitively. No
recursive scan is performed. Normal ordering compares filenames without case,
then uses case-sensitive order to break ties. Up to 512 files are accepted;
oversized folders and paths fail explicitly rather than silently omitting songs.
Reopen the player to rescan a folder after adding or removing files.

| Control | Action |
| --- | --- |
| Space | Pause/resume; start the selected track if stopped |
| n / p or Right / Left | Next / previous in current order; wraps manually |
| Up / Down | Select a row |
| Enter | Play selected track; stop if it is already current |
| s | Toggle shuffle, preserving the current song |
| r | Cycle repeat off / all / one |
| + / - | Volume in 5% steps |
| q, Esc, Ctrl+C, Ctrl+] | Exit and release audio |
| Ctrl+Z | Leave player running and return to the shell |

Shuffle uses a Fisher–Yates permutation: each track appears once per cycle.
Repeat defaults to off. Repeat all starts another cycle (reshuffling if enabled);
repeat one repeats the current song. Previous follows the current cycle's order,
not a separate cross-cycle history. Corrupt/unsupported files stop with an error;
next can skip them. MP3 and 16-bit PCM WAV use the existing Teensy decoder and
44.1 kHz stereo output conversion.

## Editing while music plays

1. Start `player --repeat all /sd/music`, then press Ctrl+Z.
2. Open `edit /sd/notes.txt`, type and save normally with Ctrl+S.
3. Press Ctrl+Z in the editor, then run `sessions`.
4. Run `fg ID` for the player to change songs or pause.
5. Suspend the player again and `fg ID` for the editor to restore its buffer.

`close ID` closes a retained player. Disconnecting its owning USB/Telnet session
or graceful shutdown stops and joins the worker before freeing its state. Audio
ownership survives suspension, so another aplay/arecord/synth/WebRadio/player
cannot take the codec. Other text apps and the other console remain usable.

The Teensy shell delivers timer events to retained apps only when they opt into
`SOLAR_OS_APP_FLAG_BACKGROUND_TICKS`. Player uses these events to reap completed
workers and advance tracks while suspended. Events run in the owning console
under its gate with the original app context; they do not switch the foreground
frame/TUI, render the player or consume editor input. Other retained apps keep
their previous behavior. Shutdown/disconnect still owns cleanup.

## Resources and lifetime

Folder paths and an ordering array live in PSRAM, growing from 16 to at most
512 path slots (160 bytes each), plus two bytes per track. The folder has no
persistent state and retains no open directory or per-track file handles.
The decoder has an on-demand 32 KiB PSRAM stack and its existing PSRAM decode
buffers, plus a 128 KiB decoded-PCM queue. A priority-3 feeder with a 2 KiB
internal stack supplies the 16 KiB internal PCM ring independently of decoder/UI
scheduling. It primes half the PSRAM queue before playback (or primes at EOF for
short files). The 255 usable stereo blocks cover about 0.74 seconds, plus the
internal ring's 0.09 seconds. This addressed underruns observed during full LCD
console redraws with the original unbuffered worker. The audio ISR/DMA stays
entirely in internal memory.
Idle player costs only small bookkeeping pointers/counters, not a permanent task,
track list, stack or decode buffers. Pause retains playback allocations.

The worker registers cancellation/pause callbacks so it never polls shell input
or takes the console gate held by a UI waiting for it to stop. Pause freezes the
PCM consumer and retains queued samples, including at EOF. Resume excludes paused
time from output timeouts. Stop/cancel/disconnect joins the suspended worker
before its stack or app state is released. `audio status` reports running/paused
state, underruns and the last decoder stack headroom. No per-song console logging
is emitted into an editor session. `audio status` also shows PCM queue allocation,
peak occupancy and feeder stack headroom.

Routine history saves defer during file playback to avoid QSPI programming
masking AudioStream interrupts. Explicit writes to flash still have that hardware
limitation; use SD for the editing/playback coexistence test. Gapless transitions
are not promised, and storage stalls can exceed the bounded playback queues.

## Validation

Host tests:

```
bash scripts/ports/test_teensy41_player_host.sh
python3 tests/ports/test_teensy41_player_controls.py
python3 tests/ports/test_teensy41_audio_memory.py
bash scripts/ports/test_teensy41_sessions_host.sh
bash scripts/ports/test_teensy41_audio_host.sh
python3 tests/test_player_seek_controls.py
python3 tests/ports/test_teensy41_manual.py
```

These cover filtering, sort order, shuffle permutation/current-track preservation,
folder limits and allocation rollback; repeat/next/previous/EOF and worker joining;
1.5-second pause without sample consumption or timeout; a simulated >500 ms
decoder stall with exact stereo sample ordering and partial-block EOF padding; retained background ticks
without replacing editor context, timer wrap and 1,000 session cycles; existing
MP3/WAV/recording paths and non-Teensy seek behavior.

Live procedure (host Python with `pyserial` and `pyte`, host `ffmpeg`, audio
shield, SD, Ethernet, idle LCD and exclusive USB required):

```
python3 scripts/ports/test_teensy41_player.py --log /tmp/teensy-player-live.json
```

The test generates quiet tones, serves only those fixtures over a temporary local
HTTP server, copies them to a unique SD folder with `curl`, and verifies hashes.
It exercises playback, pause, transport, repeat/shuffle, session ownership,
player/editor switching and saving, bad/empty folders/files, and heap recovery.
It removes its fixture folder. Digital counters establish transport behavior;
speaker/microphone quality requires separate acoustic checks.

### Installed hardware acceptance — 2026-10-05

The final legacy bench image passed `test_teensy41_player.py`: MP3/WAV folder
ordering, shuffle without duplicates, repeat off/all/one, pause longer than one
second, next/previous, volume/shuffle/repeat keys, audio ownership while suspended,
invalid folders/arguments, corrupt MP3 reporting and repeated open/close cleanup.
The corrupt-file check waits for the asynchronous error rather than assuming it
will be displayed within 500 ms.

The same USB console played music, suspended the player, edited and saved an SD
file across track transitions, returned to the player to change songs and pause,
restored the editor's contents, saved again, and resumed playback. Audio counters
reported zero underruns. Idle memory recovered exactly: 372,616 internal and
8,123,832 PSRAM bytes. Generated fixtures were removed. Evidence is retained in
`solar_os-baselines/2026-10-05-player/` beside the repository, including
`teensy-player-final-live.json` and the installed HEX/ELF.
Five subsequent one-second WAV playback/SD-recording cycles and tone cleanup
also passed with exact memory recovery (`teensy-player-audio-memory-live.json`).

Installed `teensy41_telnet_legacy` HEX SHA256:
`0a5fc7bb7904deb50174810eb36672a4ec938d04fbddec4f70a1c71f232db64c`.
Build: RAM1 431,648, RAM2 187,256, flash 1,488,736 bytes. Bench pins are unchanged.
This establishes digital playback/session behavior on the tested SD card; it
does not establish acoustic quality, gapless playback or arbitrary storage loads.
