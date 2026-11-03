# OpenDNP3-derived bounded decoder

Upstream: https://github.com/dnp3/opendnp3 (Apache-2.0), tag 3.1.2,
commit `26b4c01e4839bbbda8866655e086471c4917ee53`.
Retain LICENSE, NOTICE and source attribution when redistributing.

`crc.c` adapts `cpp/lib/src/link/CRC.cpp` to C with a const lookup table.
`functions.c` derives names/values from `cpp/lib/include/opendnp3/gen/FunctionCode.h`.
`decode.c` adapts framing/resynchronization and transport checks from
`cpp/lib/src/link/LinkLayerParser.cpp`, `LinkFrame.cpp` and
`cpp/lib/src/transport/TransportRx.cpp` into a fixed-memory passive monitor.
Object layouts are checked against the upstream generated group definitions.
Changes: C API, no executors/logging/ser4cpp/dynamic allocation, event callbacks,
CRC diagnostics, bounded per-address transport reassembly, strict sequence-loss
reset, duplicate segment suppression, and selected object-value display.

This is a decoder, not the complete OpenDNP3 master/outstation stack. It never
acknowledges frames or creates protocol requests. Relay logic is separate and
forwards the original bytes, including corrupt and unsupported traffic. Unknown
objects/qualifiers are labelled and left raw rather than guessed.

The separate SolarOS lab (`src/services/solar_os_dnp3_lab.c`) now optionally
creates requests/responses, changes supported frame contents and injects faults.
Those functions are not part of this passive decoder or a complete upstream
OpenDNP3 stack. Transparent mode remains byte-exact; explicit rewrite/fault rules
select changed forwarding. See `doc/manual/apps.md#dnp3` for the bounded simulator
and rewrite subset. Host builds also use an explicit unsigned size comparison in
transport capacity checking.
