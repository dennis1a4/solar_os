#include "mesh_internal.h"
#include <stdio.h>
#include <string.h>
static const struct solar_mesh_driver *const drivers[] = {
    &solar_mesh_meshcore_driver, &solar_mesh_meshtastic_driver};
size_t solar_mesh_client_size(void) { return sizeof(solar_mesh_client_t); }
const char *solar_mesh_driver_name(unsigned i) {
  return i < sizeof(drivers) / sizeof(drivers[0]) ? drivers[i]->name : NULL;
}
void mesh_text(char *out, size_t capacity, const char *text, size_t length) {
  if (!capacity)
    return;
  size_t n = 0;
  while (n + 1 < capacity && n < length && text[n]) {
    unsigned char ch = (unsigned char)text[n];
    out[n] = (ch < 32 || ch == 127) ? ' ' : text[n];
    ++n;
  }
  out[n] = 0;
}
void mesh_emit(solar_mesh_client_t *c, solar_mesh_event_t *e) {
  e->sequence = ++c->event_sequence;
  e->time_ms = c->now;
  c->events[(e->sequence - 1) % SOLAR_MESH_EVENTS] = *e;
  if (c->notify)
    c->notify(c->notify_user);
}
void mesh_note(solar_mesh_client_t *c, solar_mesh_event_kind_t kind,
               const char *text) {
  solar_mesh_event_t e = {0};
  e.kind = kind;
  mesh_text(e.text, sizeof(e.text), text, strlen(text));
  mesh_emit(c, &e);
}
void mesh_error(solar_mesh_client_t *c, const char *text, bool fatal) {
  mesh_text(c->status.error, sizeof(c->status.error), text, strlen(text));
  if (fatal) {
    c->failed = true;
    c->status.ready = false;
    c->waiting = 0;
    c->tx_size = c->tx_used = 0;
  }
  mesh_note(c, SOLAR_MESH_ERROR, text);
}
void mesh_node(solar_mesh_client_t *c, const char *id, const char *name,
               uint32_t seen) {
  unsigned i = 0;
  for (; i < c->status.node_count; ++i)
    if (!strcmp(id, c->nodes[i].id))
      break;
  if (i == SOLAR_MESH_NODES) {
    ++c->status.node_overflow;
    return;
  }
  if (i == c->status.node_count)
    ++c->status.node_count;
  mesh_text(c->nodes[i].id, sizeof(c->nodes[i].id), id, strlen(id));
  mesh_text(c->nodes[i].name, sizeof(c->nodes[i].name), name, strlen(name));
  c->nodes[i].last_seen = seen;
}
bool mesh_idle(const solar_mesh_client_t *c) {
  return !c->failed && !c->waiting && !c->tx_size;
}
void mesh_tx_event(solar_mesh_client_t *c, solar_mesh_event_kind_t kind,
                   uint32_t id, const char *text) {
  solar_mesh_event_t e = {0};
  e.kind = kind;
  e.packet_id = id;
  e.channel = c->pending_channel;
  e.direct = c->pending_direct;
  mesh_text(e.text, sizeof(e.text), text, strlen(text));
  mesh_emit(c, &e);
}
solar_mesh_result_t solar_mesh_init(solar_mesh_client_t *c, const char *driver,
                                    solar_mesh_write_fn write, void *user,
                                    uint32_t now, uint32_t nonce) {
  if (!c || !driver || !write)
    return SOLAR_MESH_ARGUMENT;
  const struct solar_mesh_driver *d = NULL;
  for (unsigned i = 0; i < sizeof(drivers) / sizeof(drivers[0]); ++i)
    if (!strcmp(driver, drivers[i]->name))
      d = drivers[i];
  if (!d)
    return SOLAR_MESH_UNSUPPORTED;
  memset(c, 0, sizeof(*c));
  c->driver = d;
  c->write = write;
  c->user = user;
  c->now = now;
  c->nonce = nonce ? nonce : 1;
  c->next_id = c->nonce;
  c->last_poll = now;
  snprintf(c->status.driver, sizeof(c->status.driver), "%s", driver);
  c->status.text_limit = d->text_limit;
  d->start(c);
  return SOLAR_MESH_OK;
}
void solar_mesh_set_notify(solar_mesh_client_t *c, void (*notify)(void *),
                           void *user) {
  if (c) {
    c->notify = notify;
    c->notify_user = user;
  }
}
void solar_mesh_feed(solar_mesh_client_t *c, const uint8_t *data, size_t count,
                     uint32_t now) {
  if (!c || !c->driver || c->failed || (!data && count))
    return;
  c->now = now;
  const bool mc = c->driver == &solar_mesh_meshcore_driver;
  if (c->header_used && (uint32_t)(now - c->rx_last) > 1000) {
    c->header_used = c->rx_used = c->rx_size = 0;
    ++c->status.malformed;
  }
  for (size_t i = 0; i < count; ++i) {
    uint8_t b = data[i];
    c->rx_last = now;
    if (!c->header_used) {
      if (b == (mc ? 0x3e : 0x94))
        c->header[c->header_used++] = b;
      continue;
    }
    if (!mc && c->header_used == 1 && b != 0xc3) {
      c->header_used = b == 0x94 ? 1 : 0;
      continue;
    }
    const size_t header_size = mc ? 3 : 4;
    if (c->header_used < header_size) {
      c->header[c->header_used++] = b;
      if (c->header_used == header_size) {
        c->rx_size = mc ? (c->header[1] | ((size_t)c->header[2] << 8))
                        : (((size_t)c->header[2] << 8) | c->header[3]);
        c->rx_used = 0;
        if (!c->rx_size || c->rx_size > (mc ? 255 : MESH_FRAME_MAX)) {
          c->header_used = 0;
          ++c->status.malformed;
        }
      }
      continue;
    }
    c->rx[c->rx_used++] = b;
    if (c->rx_used == c->rx_size) {
      ++c->status.frames_rx;
      c->driver->receive(c, c->rx, c->rx_size);
      c->header_used = c->rx_used = c->rx_size = 0;
    }
  }
}
void solar_mesh_tick(solar_mesh_client_t *c, uint32_t now) {
  if (!c || !c->driver || c->failed)
    return;
  c->now = now;
  if (c->header_used && (uint32_t)(now - c->rx_last) > 1000) {
    c->header_used = c->rx_used = c->rx_size = 0;
    ++c->status.malformed;
  }
  if (c->tx_size) {
    int n = c->write(c->user, c->tx + c->tx_used, c->tx_size - c->tx_used);
    if (n < 0 || (size_t)n > c->tx_size - c->tx_used) {
      mesh_error(c, "UART write failed; reconnect required", true);
      return;
    }
    c->tx_used += (size_t)n;
    if (c->tx_used == c->tx_size) {
      c->tx_size = c->tx_used = 0;
      ++c->status.frames_tx;
      c->response_started = now;
    } else if ((uint32_t)(now - c->tx_started) > 2000) {
      ++c->status.timeouts;
      mesh_error(c, "UART transmit timeout; reconnect required", true);
      return;
    }
  }
  if (!c->tx_size && c->waiting &&
      (uint32_t)(now - c->response_started) > 10000) {
    ++c->status.timeouts;
    mesh_error(c, "Radio response timeout; reconnect required", true);
    return;
  }
  c->driver->tick(c);
}
solar_mesh_result_t solar_mesh_send(solar_mesh_client_t *c, unsigned channel,
                                    const char *destination, const char *text,
                                    uint32_t timestamp) {
  if (!c || !c->driver || !text || channel >= SOLAR_MESH_CHANNELS)
    return SOLAR_MESH_ARGUMENT;
  size_t n = strlen(text);
  if (!n || n > c->driver->text_limit)
    return SOLAR_MESH_ARGUMENT;
  for (size_t i = 0; i < n; ++i)
    if ((unsigned char)text[i] < 32 || (unsigned char)text[i] == 127)
      return SOLAR_MESH_ARGUMENT;
  if (!c->status.ready || c->failed)
    return SOLAR_MESH_OFFLINE;
  if (!mesh_idle(c))
    return SOLAR_MESH_BUSY;
  return c->driver->send(c, channel, destination, text, timestamp);
}
solar_mesh_result_t solar_mesh_refresh(solar_mesh_client_t *c) {
  if (!c || !c->driver || !c->status.ready || c->failed)
    return SOLAR_MESH_OFFLINE;
  if (!mesh_idle(c))
    return SOLAR_MESH_BUSY;
  return c->driver->refresh(c);
}
void solar_mesh_status(const solar_mesh_client_t *c, solar_mesh_status_t *s) {
  if (c && s)
    *s = c->status;
}
bool solar_mesh_node(const solar_mesh_client_t *c, unsigned i,
                     solar_mesh_node_t *n) {
  if (!c || !n || i >= c->status.node_count)
    return false;
  *n = c->nodes[i];
  return true;
}
bool solar_mesh_channel(const solar_mesh_client_t *c, unsigned i,
                        solar_mesh_channel_t *n) {
  if (!c || !n || i >= SOLAR_MESH_CHANNELS)
    return false;
  *n = c->channels[i];
  return n->known;
}
bool solar_mesh_event(const solar_mesh_client_t *c, uint64_t *cursor,
                      solar_mesh_event_t *e, uint64_t *lost) {
  if (!c || !cursor || !e)
    return false;
  if (*cursor > c->event_sequence)
    *cursor = 0;
  uint64_t first = c->event_sequence > SOLAR_MESH_EVENTS
                       ? c->event_sequence - SOLAR_MESH_EVENTS
                       : 0;
  if (*cursor < first) {
    if (lost)
      *lost += first - *cursor;
    *cursor = first;
  }
  if (*cursor == c->event_sequence)
    return false;
  *e = c->events[*cursor % SOLAR_MESH_EVENTS];
  ++*cursor;
  return true;
}
const char *solar_mesh_result_name(solar_mesh_result_t r) {
  static const char *const names[] = {"OK",
                                      "invalid argument or message too long",
                                      "radio busy; retry",
                                      "radio not connected",
                                      "unsupported driver",
                                      "I/O error"};
  return (unsigned)r < 6 ? names[r] : "unknown error";
}
const char *solar_mesh_event_name(solar_mesh_event_kind_t k) {
  static const char *const names[] = {
      "status", "rx", "packet", "queued", "accepted", "delivered", "error"};
  return (unsigned)k < 7 ? names[k] : "unknown";
}
