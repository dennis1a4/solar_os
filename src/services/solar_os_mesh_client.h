#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SOLAR_MESH_TEXT 240
#define SOLAR_MESH_NODES 32
#define SOLAR_MESH_CHANNELS 8
#define SOLAR_MESH_EVENTS 32

typedef enum {
  SOLAR_MESH_OK,
  SOLAR_MESH_ARGUMENT,
  SOLAR_MESH_BUSY,
  SOLAR_MESH_OFFLINE,
  SOLAR_MESH_UNSUPPORTED,
  SOLAR_MESH_IO
} solar_mesh_result_t;
typedef enum {
  SOLAR_MESH_STATUS,
  SOLAR_MESH_MESSAGE,
  SOLAR_MESH_PACKET,
  SOLAR_MESH_TX_QUEUED,
  SOLAR_MESH_TX_ACCEPTED,
  SOLAR_MESH_TX_DELIVERED,
  SOLAR_MESH_ERROR
} solar_mesh_event_kind_t;
typedef struct {
  uint64_t sequence;
  uint32_t time_ms, timestamp, packet_id;
  solar_mesh_event_kind_t kind;
  unsigned channel;
  bool direct, has_rssi, has_snr;
  int rssi, snr_q4;
  char sender[65], text[SOLAR_MESH_TEXT + 1];
} solar_mesh_event_t;
typedef struct {
  char id[65], name[40];
  uint32_t last_seen;
} solar_mesh_node_t;
typedef struct {
  bool known, enabled;
  char name[33];
} solar_mesh_channel_t;
typedef struct {
  bool ready;
  char driver[16], name[40], node_id[65], firmware[48], model[48], error[96];
  uint32_t frames_rx, frames_tx, messages_rx, messages_tx, malformed, timeouts,
      node_overflow;
  unsigned node_count, channel_count, text_limit;
  bool has_rssi, has_snr;
  int rssi, snr_q4;
  uint32_t radio_rx, radio_tx;
} solar_mesh_status_t;
// Return accepted bytes (including short writes), zero for backpressure, -1 on
// failure.
typedef int (*solar_mesh_write_fn)(void *user, const uint8_t *bytes,
                                   size_t count);
typedef struct solar_mesh_client solar_mesh_client_t;
size_t solar_mesh_client_size(void);
const char *solar_mesh_driver_name(unsigned index);
solar_mesh_result_t solar_mesh_init(solar_mesh_client_t *client,
                                    const char *driver,
                                    solar_mesh_write_fn write, void *user,
                                    uint32_t now_ms, uint32_t nonce);
// Called synchronously after publishing an event. Must not block or reenter the
// client.
void solar_mesh_set_notify(solar_mesh_client_t *client, void (*notify)(void *),
                           void *user);
void solar_mesh_feed(solar_mesh_client_t *client, const uint8_t *bytes,
                     size_t count, uint32_t now_ms);
void solar_mesh_tick(solar_mesh_client_t *client, uint32_t now_ms);
solar_mesh_result_t solar_mesh_send(solar_mesh_client_t *client,
                                    unsigned channel, const char *destination,
                                    const char *text, uint32_t timestamp);
solar_mesh_result_t solar_mesh_refresh(solar_mesh_client_t *client);
void solar_mesh_status(const solar_mesh_client_t *client,
                       solar_mesh_status_t *status);
bool solar_mesh_node(const solar_mesh_client_t *client, unsigned index,
                     solar_mesh_node_t *node);
bool solar_mesh_channel(const solar_mesh_client_t *client, unsigned index,
                        solar_mesh_channel_t *channel);
// Independent reader cursors; returns the oldest retained event on overrun and
// counts losses.
bool solar_mesh_event(const solar_mesh_client_t *client, uint64_t *cursor,
                      solar_mesh_event_t *event, uint64_t *lost);
const char *solar_mesh_result_name(solar_mesh_result_t result);
const char *solar_mesh_event_name(solar_mesh_event_kind_t kind);
#ifdef __cplusplus
}
#endif
