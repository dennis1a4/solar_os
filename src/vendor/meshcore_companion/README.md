# MeshCore companion codec

Portable C99 codec from https://github.com/SH3D/meshcore_c, version 0.2.2,
commit `b9e727ade800eadbfbc5d97b0094f21417473d54`. MIT license, preserved in LICENSE.
This is the companion UART client, separate from SolarOS's on-board radio stack.

Local corrections:
- Require all 147 contact-body bytes, including `lastmod`, before reading them.
- V3 text prefixes are SNR plus two reserved bytes, not RSSI. Report RSSI unavailable.

The SolarOS adapter uses its own bounded streaming framer. Regression tests:
`bash scripts/ports/test_teensy41_mesh_host.sh`.
