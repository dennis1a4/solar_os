# Telnet server port — 2026-09-28

The Teensy Ethernet port now accepts one password-authenticated Telnet client,
with its own SolarOS shell alongside USB and LCD. The server is off after boot
and starts explicitly; no automatic startup or saved server setting is added.
Telnet is plaintext, including the password; use it on a trusted LAN.

## Starting and connecting

Create `/flash/telnet.pass` with Edit, containing your password on one line
(1–63 printable ASCII characters, optional final newline). Avoid putting the
password directly in a shell command/history. Then, from USB or LCD:

```text
network up
network status
telnetd start /flash/telnet.pass
telnetd status
```

From a computer, use `telnet <address-shown-by-network-status>`. The observed
DHCP address during testing was `192.168.1.197`; DHCP may assign another address.
The password prompt takes the file's password; there is no separate username.

An optional port follows the filename, e.g. `telnetd start /flash/telnet.pass 2323`.
Use `telnetd stop` to close the listener/client. Stop then start to reload the
password or change ports. `exit` closes only the remote shell. No anonymous mode,
TLS/SSH server, multiple-client support or privilege separation is implemented.
An authenticated shell has the same command/file capabilities as the local shell.

The file is read at startup and then closed; its contents are retained in RAM
until stop/reboot. The daemon does not display the password. The filesystem has
no credential-specific access control: other full-access shells can read it.

## Port integration

Upstream `jobs/solar_os_telnetd_job.c` depends on BSD sockets, WiFi latency leases,
background-job services and dynamic port-shell sessions unavailable in this port.
The Teensy adapter preserves its negotiation/NVT approach in the bounded,
transport-independent `solar_os_telnet_codec.*`, and uses the existing Teensy
shell lifecycle. The full upstream job is unchanged. On Teensy, control is via
`telnetd start|status|stop`, not `job start telnetd`.

- QNEthernet remains confined to the existing Ethernet worker. New copied RPCs
  start/stop/accept the listener; accepted sockets are service-owned, so Python
  socket cleanup cannot close Telnet. A second client receives a busy response.
- Listener uses address reuse so closed TCP sessions do not prevent immediate
  rebinding. Link/network loss ends the active session; the enabled listener
  resumes when the network returns. Reconnection requires authentication again.
- Input handles IAC escaping, CR-LF/CR-NUL, terminal type and bounded NAWS window
  sizes (20–300 columns, 8–120 rows). Unsupported options are refused. Oversized
  subnegotiation is discarded. Output escapes IAC and encodes NVT line endings.
- Login has a 30-second deadline, no password echo and rejects overlong input.
  A stalled output peer is disconnected after two seconds of backpressure.
  Input buffering is bounded and applies TCP backpressure rather than dropping
  ordinary shell bytes. Incomplete negotiation counts toward the login deadline.
- Third console has a reserved 40 KiB OCRAM task stack and uses the existing
  recursive console gate. Filesystem/app calls remain serialized across consoles;
  synchronous apps yield through the existing cancellation hooks.
- Remote shell state is allocated on successful login and freed on disconnect.
  The entire app/child chain is stopped without resuming parents; app reservations
  are released. Reconnect starts a fresh shell/cwd. The listener/task itself stays
  available for future clients, even after stop, using reserved static memory.
- Text apps use the remote terminal. Graphical apps require the local LCD.
  Audio ownership also distinguishes remote, USB and LCD sessions. Remote NAWS
  geometry never overwrites saved USB terminal dimensions.

## Firmware and pin compatibility

`teensy41_display` includes Telnet plus the planned scope pin move (ADC40/AmpEn0).
Do not upload that profile to the old wiring.

`teensy41_telnet_legacy` is the current-wiring test profile: AmpEn remains pin 40,
Serial1 remains enabled, and the scope ADC pin is unassigned. It includes the
scope demo and pdpower demo; physical ADC use remains blocked. Both profiles
build; the legacy profile was uploaded for Telnet device testing. No physical
wiring was changed. After the AmpEn move, use the normal display profile.

## Tests and evidence

```sh
bash scripts/ports/test_teensy41_telnet_host.sh
python3 scripts/ports/test_teensy41_telnet.py --log /tmp/teensy-telnet-device-final.json
```

Host tests run the codec and actual Teensy service against a mock RPC transport
under ASan/UBSan: fragmented negotiation, invalid/oversized NAWS, escaped IAC,
line endings, arbitrary input, failed/overlong login, backspace, timeout, partial
writes, stalled sends and 100 reconnects. The manual-generator tests also pass.
They do not exercise the actual QNEthernet listener or shell task.

The device runner owns USB serial and uses actual Ethernet TCP connections. It
creates one unique temporary password file, redacts the password from its report,
stops the test server and removes the file on completion. It changes Ethernet
up/down state; run only with idle shells and no unrelated network workload.
The first run's Files assertion assumed a filename would be on the first page;
it now checks the application footer. Cleanup now uses shell `rm`, since this
MicroPython build lacks `os`. A subsequent run found address-in-use on immediate
server restart; the address-reuse fix is included in the tested firmware.

Build/upload logs: `/tmp/teensy-telnet-build.log`,
`/tmp/teensy-telnet-legacy-build.log`, `/tmp/teensy-telnet-upload.log`.
Longer network soak, physical Ethernet cable removal, additional Telnet clients,
remote audio playback and resource-exhaustion tests remain in the
[master test checklist](teensy41-test-checklist.md).

### Observed device result

Full device run passed: `/tmp/teensy-telnet-device-final.json`. Verified failed
login, initial/live NAWS, shell commands, graphical rejection, independent cwd,
busy-client refusal, Files cleanup/relaunch, cancellation of an infinite Python
loop on disconnect, fresh-shell reconnect, remote exit, ten reconnect cycles,
network down/up recovery, stop disconnect and three immediate stop/start cycles.
SD/USB remained mounted and the keyboard reported connected. Physical keyboard
input, cable pulls and LCD appearance were not checked in this run.

Warm idle memory, after ten reconnects and after server stop, was identical:
30,720 / 79,104 internal heap bytes free and 8,202,480 / 8,388,608 PSRAM bytes
free. The cold initial internal reading was 30,756 bytes (36-byte one-time warmup).
At the end of automation, the server was stopped, the temporary password file
was removed, and Ethernet was left up. The user subsequently confirmed their
own Telnet connection worked.

Installed legacy image: flash 1,294,904 bytes (15.9%), RAM1 allocation 435,616
bytes (83.1%), RAM2 271,168 bytes (51.7%). The normal new-wiring display candidate
also builds: flash 1,292,916, RAM1 435,488 and RAM2 271,168 bytes. Static allocations
and runtime free-heap figures describe different pools; do not add them together.

### Session wrap-up — 2026-09-28

After the user's successful connection, USB serial confirmed the server stopped
with no client. Ethernet was stopped and SD/USB storage safely ejected. User
files were preserved. The board is ready to unplug; networking and Telnet must
be explicitly started next time. See the [handoff](teensy41-handoff.md).
