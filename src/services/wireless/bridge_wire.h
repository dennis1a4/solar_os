/* SolarOS bridge wire v1. Byte fields and little-endian integers; no ABI
 * structs. */
#ifndef SOLAR_BRIDGE_WIRE_H
#define SOLAR_BRIDGE_WIRE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define WB_SPI_MODE 3u
#define WB_SPI_HZ 6000000u
#define WB_SIZE 1024u
#define WB_HEADER 16u
#define WB_PAYLOAD (WB_SIZE - WB_HEADER)
enum {
  WB_NOP = 0,
  WB_STATUS = 1,
  WB_WIFI_SCAN = 2,
  WB_WIFI_RESULT = 3,
  WB_BT_SCAN = 4,
  WB_BT_RESULT = 5,
  WB_HID_CONNECT = 6,
  WB_AUDIO_CONNECT = 7,
  WB_PAIR = 8,
  WB_HID_CLOSE = 9,
  WB_AUDIO_CLOSE = 10,
  WB_AUDIO_START = 11,
  WB_AUDIO_PCM = 12,
  WB_AUDIO_STOP = 13,
  WB_KEYS = 14,
  WB_WIFI_JOIN = 15, WB_WIFI_LEAVE = 16, WB_WIFI_LINK = 17,
  WB_WIFI_TX = 18, WB_WIFI_RX = 19, WB_WIFI_SAVE = 20,
  WB_WIFI_RECONNECT = 21, WB_WIFI_FORGET = 22
};
enum {
  WB_OK = 0,
  WB_INVALID = 1,
  WB_BUSY = 2,
  WB_FAILED = 3,
  WB_NOT_READY = 4,
  WB_UNSUPPORTED = 5
};
/* Status: flags[0], wifi_count[1], bt_count[2], pair_kind[3], passkey[4..7],
 pcm_free[8..11], pcm_underruns[12..15], pair_bda[16..21], key
 generation[22..25]. Flags: wifi scanning=1, BT scanning=2, HID connected=4,
 audio connected=8, audio streaming=16, audio requested=32. Pair: BT
 confirm=1/passkey=2/PIN=3, BLE confirm=4/passkey=5; display-only passkey=6. */
static inline uint16_t wb_u16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t wb_u32(const uint8_t *p) {
  return (uint32_t)wb_u16(p) | ((uint32_t)wb_u16(p + 2) << 16);
}
static inline void wb_p16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static inline void wb_p32(uint8_t *p, uint32_t v) {
  wb_p16(p, (uint16_t)v);
  wb_p16(p + 2, (uint16_t)(v >> 16));
}
static inline uint16_t wb_crc(const uint8_t *p, size_t n) {
  uint16_t c = 0xffff;
  for (size_t i = 0; i < n; i++) {
    uint8_t b = (i == 12 || i == 13) ? 0 : p[i];
    c ^= (uint16_t)b << 8;
    for (unsigned j = 0; j < 8; j++)
      c = (uint16_t)((c << 1) ^ ((c & 0x8000) ? 0x1021 : 0));
  }
  return c;
}
static inline int wb_valid(const uint8_t *p, size_t n) {
  return n == WB_SIZE && p[0] == 'S' && p[1] == 'W' && p[2] == 1 && p[3] <= 1 &&
         wb_u16(p + 8) <= WB_PAYLOAD &&
         wb_u16(p + 12) == wb_crc(p, WB_HEADER + wb_u16(p + 8));
}
static inline void wb_pack(uint8_t *p, uint8_t response, uint16_t seq,
                           uint16_t op, uint16_t status, const void *data,
                           size_t len) {
  memset(p, 0, WB_SIZE);
  if (len > WB_PAYLOAD)
    return;
  p[0] = 'S';
  p[1] = 'W';
  p[2] = 1;
  p[3] = response;
  wb_p16(p + 4, seq);
  wb_p16(p + 6, op);
  wb_p16(p + 8, (uint16_t)len);
  wb_p16(p + 10, status);
  if (len)
    memcpy(p + WB_HEADER, data, len);
  wb_p16(p + 12, wb_crc(p, WB_HEADER + len));
}
#endif
