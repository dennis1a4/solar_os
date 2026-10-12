#pragma once
#include "solar_os_mesh_client.h"
#define MESH_FRAME_MAX 512
struct solar_mesh_driver {
  const char *name;
  unsigned text_limit;
  void (*start)(solar_mesh_client_t *);
  void (*receive)(solar_mesh_client_t *, const uint8_t *, size_t);
  void (*tick)(solar_mesh_client_t *);
  solar_mesh_result_t (*send)(solar_mesh_client_t *, unsigned, const char *,
                              const char *, uint32_t);
  solar_mesh_result_t (*refresh)(solar_mesh_client_t *);
};
struct solar_mesh_client {
  const struct solar_mesh_driver *driver;
  solar_mesh_write_fn write;
  void *user;
  void (*notify)(void *);
  void *notify_user;
  solar_mesh_status_t status;
  solar_mesh_node_t nodes[SOLAR_MESH_NODES];
  solar_mesh_channel_t channels[SOLAR_MESH_CHANNELS];
  solar_mesh_event_t events[SOLAR_MESH_EVENTS];
  uint64_t event_sequence;
  uint8_t rx[MESH_FRAME_MAX], header[4], tx[MESH_FRAME_MAX + 4];
  size_t rx_used, rx_size, header_used, tx_size, tx_used;
  uint32_t now, rx_last, tx_started, response_started, last_poll, nonce,
      next_id;
  uint8_t waiting, phase, next_channel;
  bool drain, refresh, failed;
  uint32_t pending_id, pending_ack, self_number;
  struct {
    uint32_t id, destination, sent;
  } receipts[4];
  unsigned pending_channel;
  bool pending_direct;
  char pending_text[SOLAR_MESH_TEXT + 1];
};
extern const struct solar_mesh_driver solar_mesh_meshcore_driver,
    solar_mesh_meshtastic_driver;
void mesh_text(char *out, size_t capacity, const char *text, size_t length);
void mesh_emit(solar_mesh_client_t *c, solar_mesh_event_t *event);
void mesh_note(solar_mesh_client_t *c, solar_mesh_event_kind_t kind,
               const char *text);
void mesh_error(solar_mesh_client_t *c, const char *text, bool fatal);
void mesh_node(solar_mesh_client_t *c, const char *id, const char *name,
               uint32_t seen);
bool mesh_idle(const solar_mesh_client_t *c);
void mesh_tx_event(solar_mesh_client_t *c, solar_mesh_event_kind_t kind,
                   uint32_t id, const char *text);
