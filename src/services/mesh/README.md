# Companion radio clients

Shared portable API: `../solar_os_mesh_client.h`. Fixed-capacity, no allocation or
I/O in the core; callers supply storage and a short-write-aware transport callback.
Drivers are compiled in and selected by name at runtime. This is not a dynamic
ELF loader and does not implement an on-board LoRa radio stack.

MeshCore uses the MIT `meshcore_c` codec. Meshtastic uses nanopb with a deliberately
small schema for text, nodes, channels, firmware identity, queue/routing results,
and keepalive. No Meshtastic Arduino implementation or full generated schema is
linked. Wire field numbers and framing were checked against the official
Meshtastic Arduino client at `72b841285f8411c1ca9b945bd5b35cf344396eec` and its
protobuf revision `7b2464c9b8c1521f93852261e4123826e5b25e11`:
https://github.com/meshtastic/Meshtastic-arduino
https://github.com/meshtastic/protobufs

Regenerate with protoc and nanopb generator 0.4.9.1:

```sh
python3 /path/to/nanopb/generator/nanopb_generator.py \
  -I src/services/mesh -D src/services/mesh src/services/mesh/meshtastic_client.proto
```

`bash scripts/ports/test_teensy41_mesh_host.sh` checks both drivers with simulated
serial input, independent golden handshake bytes, short writes, invalid lengths,
truncated records, timeouts, node/channel discovery, text and delivery states,
independent event readers, and randomized malformed traffic under ASan/UBSan.
Hardware UART and over-the-air testing require the expansion boards.

Teensy integration: `mesh_service.cpp` owns UART claims and PSRAM clients; one lazy
8 KiB internal-preferred worker stack polls all radios. Logging allocates a separate lazy
8 KiB internal-preferred stack and writes outside the radio mutex. These worker stacks stay
allocated until reboot; idle workers block on notifications. Each radio needs
about 16.5 KiB of PSRAM, released on disconnect. Each chat/monitor view has its own
PSRAM history. Neither driver stores radio keys in its retained status/history.

Connections, logging and bridging are not persisted across reboot. Existing radio
firmware handles encryption, routing, modem/region configuration and channel keys.
The current subset does not edit those settings, upload firmware, or implement
position/telemetry configuration. MeshCore direct messages do not use a channel;
the common `mesh dm` channel argument is ignored by that driver.

## Background service notifications

Applications use `../solar_os_mesh_service.h`, not the UART or protocol parser.
Subscribe with a slot bitmask, then `solar_os_mesh_wait(subscription, timeout_ms)`
returns the changed-slot bits. A worker can block; a foreground app uses a zero
wait in its normal event handler. The service publishes to the per-radio history
before setting the FreeRTOS event-group bits. Bursts coalesce into one wake-up;
consumers drain all queued events using independent cursors and get explicit
lost-event counts. One app cannot consume another app's messages.

Chat and monitor subscribe on start and unsubscribe on stop. Logging has its own
subscription and writer task. Disconnect notifications wake readers too; closing
an app never disconnects the shared radio. Up to eight subscriptions are supported.
Subscription memory is PSRAM; consumers must stop waiting before unsubscribing.
