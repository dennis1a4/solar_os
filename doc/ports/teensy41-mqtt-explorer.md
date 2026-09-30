# MQTT Explorer on Teensy 4.1

The `teensy41_display` profile includes the native `mqttx` application. It runs
on the local LCD/USB-host keyboard or USB serial TUI, with one capture worker.

```text
network up
mqttx broker.local:1883
mqttx broker.local:1883 --auth /flash/mqtt-auth.txt
mqttx broker.local:1883 --auth /flash/mqtt-auth.txt --log /sd/capture-001.jsonl
```

The optional auth file contains username and password on separate lines.
Credentials are read at launch and not included in the app display or capture
log. They are cleared from app/client configuration memory during cleanup.
The auth file itself remains on the chosen storage volume. Avoid placing it in
source-control or firmware backups. New logs use exclusive creation and never
overwrite existing files. This required adding standard `fopen("wx")` support
to the Teensy storage adapter; existing open modes retain their behavior.

## Interface

- Left pane: collapsible topic hierarchy, per-topic received count and latest
  payload preview. Intermediate path components count towards the node limit.
- Tab switches the left pane to chronological history, newest first. Home
  follows new arrivals; selecting older messages holds that selection while
  it remains in the ring.
- Right pane: selected topic, sequence, receive uptime, original payload size,
  delivered QoS, retained/duplicate/truncation flags, and payload.
- Up/Down select; Left/Right collapse/expand; `/` edits a case-sensitive topic
  substring filter; `c` clears it. `x` toggles hex; `j` formats JSON-like text
  (a display convenience, not JSON validation). `[`/`]` scroll payload details.
- Space freezes automatic screen refresh. The capture worker and SD logging
  continue. Manual navigation can refresh the paused view.
- Q, Escape or Ctrl-] closes the app, drains available log entries and releases
  its network session, worker, TUI and PSRAM.

Network text is sanitized before terminal rendering; binary payloads can be
inspected in hex. The text display remains ASCII-oriented on this port.

## Capture semantics and limits

This is a broker subscriber, not a packet sniffer. It requests QoS 1 for `#`
and `$SYS/#`, accepts QoS 0/1 deliveries, acknowledges QoS 1 and displays DUP
rather than silently deduplicating. If one subscription is denied it still
captures the accepted subscription and displays the denial. Both denied,
authentication failure or malformed packets leave an error and trigger retry.

The initial client implements MQTT 3.1.1 over IPv4/DNS TCP, optional username/
password, clean sessions, keepalive and reconnect. TLS, MQTT 5, WebSockets,
publishing and persistent offline delivery are not implemented. Only messages
allowed and delivered by the broker appear. Previously retained values arrive
on subscription; arbitrary earlier history and messages during connection gaps
cannot be reconstructed.

The PSRAM model keeps 256 history records and 256 tree nodes. Each record stores
up to 255 topic bytes and 2,048 payload bytes, with original lengths and explicit
truncation. Latest per-topic records survive history eviction. Topics too long
to identify without truncation are retained in history but not merged into the
tree. Once node capacity is exhausted, new topics still enter history. Counters
show received messages, history eviction, truncation, unindexed messages and
detected connection interruptions. These limits bound memory rather than
promising lossless capture at arbitrary broker rates.

The incremental wire decoder handles any TCP fragmentation, streams/discards
excess payload after retaining its prefix, and rejects packets above 1 MiB.
It never allocates according to an untrusted packet's advertised length.

Optional JSON-lines logs contain sequence, receive uptime, original lengths,
QoS/flags, an escaped topic, exact captured `topic_hex` bytes and `payload_hex`.
The final `capture_end` record reports `log_lost`. Logging runs under the
console/storage serialization while the worker continues receiving. A slow SD
card can fall behind the ring; skipped records are counted. The log cannot
recover data missed before connection, during network gaps, or discarded past
capture-prefix limits. Log-write errors stop logging while capture continues.

## Architecture and tests

The app and capture/codec/model sources are shared SolarOS C, registered as
package `app_mqtt_explorer`. They use the existing managed network-session API
rather than ESP-specific MQTT transport. The older ESP MQTT shell service is
unchanged. The Teensy build and hardware are the validated target; an ESP build
has not been claimed tested.

The capture worker uses the existing foreground-worker allocator. On Teensy
this resource is shared with SSH, Playground operations and audio; concurrent
worker launches can be rejected. The UI never holds the model mutex while
rendering to the panel or writing SD files. The topic walk is iterative so a
deep topic cannot exhaust the console stack.

```sh
bash scripts/ports/test_teensy41_mqtt_host.sh
python3 scripts/ports/test_teensy41_mqtt.py --log /tmp/teensy-mqtt-fixture-final.json
python3 scripts/ports/test_teensy41_mqtt.py --live-only --live-host BROKER_IP \
  --live-auth /private/credentials-file --log /tmp/teensy-mqtt-live.json
```

Host tests use ASAN/UBSAN for codec vectors, every split point of a 4 KiB
publication, byte-at-a-time delivery, binary/empty/oversized payloads, malformed
lengths, topic limits, history eviction and deterministic fuzz input.
The isolated hardware broker checks subscriptions, QoS 1 ACKs, keepalive,
reconnect, display pause, payload views/filtering, a 300-message burst, exclusive
log creation, log contents and repeated lifecycle cleanup. The optional live
mode connects read-only, saves a uniquely named auth file on `/flash`, and leaves
the explorer running. Credentials and credential-upload commands are excluded
from the test log. Keep the local keyboard idle during automated tests.

Validation results and the final recovery checkpoint are recorded in the
[handoff](teensy41-handoff.md).

## 2026-09-28 validation

The final fixture suite passed with 305 received/logged messages, zero log loss,
QoS 1 acknowledgment, keepalive, reconnect and exact heap/PSRAM recovery after
repeated app lifecycles (36,964 / 8,202,576 bytes free). The deliberate burst
produced 49 ring evictions, 52 unindexed messages at the 256-node ceiling and
one explicitly truncated 4 KiB payload. Existing-log overwrite was rejected.
The final firmware also passed the dual-console regression and authenticated
read-only capture from the user's real broker. Firmware and logs are saved in
`../solar_os-baselines/2026-09-28-mqtt-explorer/`. The user also confirmed readable live LCD output, working topic selection and
Tab switching to message history. Validation of this initial release is complete.
Changes remain local and uncommitted.
