# Teensy 4.1 interactive recorder

The shared `recorder` app runs as a terminal UI on LCD, USB and Telnet.

```
recorder /sd/take.wav
recorder /sd
```

With a directory or no argument, names are generated automatically. The default
folder is `/sd`. Existing files are refused. Capture uses the SGTL5000 microphone
at 44.1 kHz, mono, signed 16-bit PCM WAV; format fields are fixed on this port.

- R or Enter: start/stop recording; S: stop and finalize the WAV.
- Space: pause/resume. Captured samples are drained and discarded while paused.
- P: replay the last recording; M: toggle microphone monitoring.
- Up/Down: output volume. Tab: setup, including filename, folder and input gain.
- Esc/Q/Ctrl+]: exit, stopping and saving any recording first.
- Ctrl+Z: retain the app and keep recording. `sessions`, `fg ID`, `close ID`
  provide normal retained-session controls.

Settings are stored in `/sd/.recorder/settings.bin`. Audio ownership includes
retained sessions, preventing other audio apps from taking the codec. The worker
uses a 32 KiB PSRAM stack, with explicit completion and join before deletion.
SD capture uses the existing bounded PSRAM spool and internal capture ring.
Pausing keeps draining input; cancellation finalizes a playable partial WAV.
Capture overrun, write failure or storage loss stops the operation with an error.
The existing one-hour capture limit applies. A power loss cannot finalize a WAV.

Validation: `scripts/ports/test_teensy41_recorder.py --log REPORT.json` requires
an idle Teensy, mounted SD and audio shield. It uses unique temporary files and
checks capture, pause, background close, valid WAV headers, playback, monitoring,
file protection, and repeated allocation recovery. Audio ring failure-injection
coverage remains in `tests/ports/test_teensy41_audio_memory.py`.

The complementary `test_teensy41_recorder_serial.py` test reconstructs the USB
terminal with pyte and exercises gain setup, monitored recording, pause and
invalid output paths. Teensy recorder diagnostics do not write over an active
terminal UI. Save failures remain errors even when Stop requested the save.

The host test `tests/ports/test_teensy41_recorder_transport.py` compiles the actual
capture transport under ASan/UBSan with deterministic microphone input. It checks
paused samples are omitted, monitoring duplicates mono to stereo, cancellation
finalizes the header, existing files are unchanged, unsupported formats are
rejected, and capture/drop/sync errors release allocations.
