#include "mesh_internal.h"
#include "meshtastic_client.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool transmit(solar_mesh_client_t *c, const solar_mesh_ToRadio *m) {
  pb_ostream_t stream = pb_ostream_from_buffer(c->tx + 4, MESH_FRAME_MAX);
  if (!pb_encode(&stream, solar_mesh_ToRadio_fields, m)) {
    mesh_error(c, "Cannot encode radio command", false);
    return false;
  }
  c->tx[0] = 0x94;
  c->tx[1] = 0xc3;
  c->tx[2] = (uint8_t)(stream.bytes_written >> 8);
  c->tx[3] = (uint8_t)stream.bytes_written;
  c->tx_size = stream.bytes_written + 4;
  c->tx_used = 0;
  c->tx_started = c->now;
  return true;
}
static void start(solar_mesh_client_t *c) {
  solar_mesh_ToRadio m = solar_mesh_ToRadio_init_zero;
  m.which_payload = solar_mesh_ToRadio_want_config_tag;
  m.payload.want_config = c->nonce;
  c->status.ready = false;
  c->last_poll = c->now;
  c->waiting = 1;
  c->response_started = c->now;
  transmit(c, &m);
  mesh_note(c, SOLAR_MESH_STATUS, "Requesting Meshtastic configuration");
}
static void node_id(char *out, size_t cap, uint32_t number) {
  snprintf(out, cap, "!%08lx", (unsigned long)number);
}
static void receive(solar_mesh_client_t *c, const uint8_t *bytes,
                    size_t count) {
  solar_mesh_FromRadio m = solar_mesh_FromRadio_init_zero;
  pb_istream_t stream = pb_istream_from_buffer(bytes, count);
  if (!pb_decode(&stream, solar_mesh_FromRadio_fields, &m)) {
    ++c->status.malformed;
    return;
  }
  if (c->waiting == 1 && (m.which_payload == solar_mesh_FromRadio_self_tag ||
                          m.which_payload == solar_mesh_FromRadio_node_tag ||
                          m.which_payload == solar_mesh_FromRadio_channel_tag ||
                          m.which_payload == solar_mesh_FromRadio_metadata_tag))
    c->response_started = c->now;
  switch (m.which_payload) {
  case solar_mesh_FromRadio_self_tag:
    c->self_number = m.payload.self.number;
    node_id(c->status.node_id, sizeof(c->status.node_id), c->self_number);
    break;
  case solar_mesh_FromRadio_node_tag: {
    solar_mesh_Node *n = &m.payload.node;
    char id[16];
    node_id(id, sizeof(id), n->number);
    if (n->has_number)
      mesh_node(c, id, n->user.name, n->heard);
    if (n->number == c->self_number)
      mesh_text(c->status.name, sizeof(c->status.name), n->user.name,
                sizeof(n->user.name));
    break;
  }
  case solar_mesh_FromRadio_metadata_tag:
    mesh_text(c->status.firmware, sizeof(c->status.firmware),
              m.payload.metadata.firmware, sizeof(m.payload.metadata.firmware));
    snprintf(c->status.model, sizeof(c->status.model), "hardware model %lu",
             (unsigned long)m.payload.metadata.model);
    break;
  case solar_mesh_FromRadio_channel_tag: {
    solar_mesh_Channel *ch = &m.payload.channel;
    if (ch->index < 0 || ch->index >= SOLAR_MESH_CHANNELS)
      break;
    solar_mesh_channel_t *out = &c->channels[ch->index];
    if (!out->known)
      ++c->status.channel_count;
    out->known = true;
    out->enabled = ch->role != 0;
    mesh_text(out->name, sizeof(out->name), ch->settings.name,
              sizeof(ch->settings.name));
    break;
  }
  case solar_mesh_FromRadio_config_complete_tag:
    if (c->waiting == 1 && m.payload.config_complete == c->nonce &&
        c->self_number) {
      c->waiting = 0;
      c->status.ready = true;
      c->status.error[0] = 0;
      mesh_note(c, SOLAR_MESH_STATUS, "Meshtastic ready");
    }
    break;
  case solar_mesh_FromRadio_rebooted_tag:
    // Do not resend an outstanding message after a reboot: delivery is unknown.
    mesh_error(c, "Radio rebooted; reconnect required", true);
    break;
  case solar_mesh_FromRadio_queue_tag: {
    solar_mesh_Queue *q = &m.payload.queue;
    if (c->waiting == 2 && q->id == c->pending_id) {
      c->waiting = 0;
      if (q->result != 0 && q->result != 35) {
        for (unsigned i = 0; i < 4; ++i)
          if (c->receipts[i].id == q->id)
            c->receipts[i].id = 0;
        char error[80];
        snprintf(error, sizeof(error),
                 "Radio rejected transmit queue request: %ld", (long)q->result);
        mesh_error(c, error, false);
      } else
        mesh_tx_event(c, SOLAR_MESH_TX_ACCEPTED, q->id,
                      "Radio accepted message; delivery unconfirmed");
    }
    break;
  }
  case solar_mesh_FromRadio_packet_tag: {
    solar_mesh_Packet *p = &m.payload.packet;
    solar_mesh_event_t e = {0};
    e.kind = SOLAR_MESH_PACKET;
    e.packet_id = p->id;
    e.channel = p->channel;
    e.timestamp = p->received;
    e.direct = p->has_destination && p->destination != UINT32_MAX;
    e.has_rssi = p->has_rssi;
    e.rssi = p->rssi;
    e.has_snr =
        p->has_snr && isfinite(p->snr) && p->snr >= -128 && p->snr <= 128;
    if (e.has_snr)
      e.snr_q4 = (int)(p->snr * 4);
    node_id(e.sender, sizeof(e.sender), p->source);
    if (e.has_rssi) {
      c->status.has_rssi = true;
      c->status.rssi = e.rssi;
    }
    if (e.has_snr) {
      c->status.has_snr = true;
      c->status.snr_q4 = e.snr_q4;
    }
    if (p->has_decoded && p->decoded.port == 1 && p->decoded.has_payload) {
      e.kind = SOLAR_MESH_MESSAGE;
      mesh_text(e.text, sizeof(e.text), (const char *)p->decoded.payload.bytes,
                p->decoded.payload.size);
      ++c->status.messages_rx;
    } else if (p->has_decoded && p->decoded.port == 5 &&
               p->decoded.has_request_id) {
      solar_mesh_Routing routing = solar_mesh_Routing_init_zero;
      pb_istream_t r = pb_istream_from_buffer(p->decoded.payload.bytes,
                                              p->decoded.payload.size);
      if (!pb_decode(&r, solar_mesh_Routing_fields, &routing)) {
        ++c->status.malformed;
        break;
      }
      if (!routing.has_error) {
        snprintf(e.text, sizeof(e.text), "Routing packet without ACK/NAK");
        mesh_emit(c, &e);
        break;
      }
      e.packet_id = p->decoded.request_id;
      // Broadcast/local routing ACKs only confirm radio handling, not remote
      // receipt.
      int receipt = -1;
      for (unsigned i = 0; i < 4; ++i)
        if (c->receipts[i].id == e.packet_id && e.packet_id)
          receipt = (int)i;
      bool addressed_to_us =
          p->has_destination && p->destination == c->self_number;
      bool delivered = receipt >= 0 && addressed_to_us &&
                       p->source == c->receipts[receipt].destination;
      e.kind =
          routing.error ? SOLAR_MESH_ERROR
          : delivered   ? SOLAR_MESH_TX_DELIVERED
          : addressed_to_us && (receipt >= 0 || c->pending_id == e.packet_id)
              ? SOLAR_MESH_TX_ACCEPTED
              : SOLAR_MESH_PACKET;
      if (receipt >= 0 && addressed_to_us && (routing.error || delivered))
        c->receipts[receipt].id = 0;
      snprintf(e.text, sizeof(e.text), "Routing result %lu for packet %08lx",
               (unsigned long)routing.error, (unsigned long)e.packet_id);
      if (c->waiting == 2 && e.packet_id == c->pending_id)
        c->waiting = 0;
    } else
      snprintf(e.text, sizeof(e.text), "%s packet, port %lu",
               p->has_decoded ? "Decoded" : "Encrypted",
               (unsigned long)p->decoded.port);
    mesh_emit(c, &e);
    break;
  }
  default:
    break; // Config fields outside this client's API are skipped by nanopb.
  }
}
static void tick(solar_mesh_client_t *c) {
  if (c->waiting == 1 && (uint32_t)(c->now - c->last_poll) > 60000) {
    ++c->status.timeouts;
    mesh_error(c, "Configuration did not complete within 60 seconds", true);
    return;
  }
  for (unsigned i = 0; i < 4; ++i)
    if (c->receipts[i].id &&
        (uint32_t)(c->now - c->receipts[i].sent) > 120000) {
      solar_mesh_event_t e = {0};
      e.kind = SOLAR_MESH_STATUS;
      e.packet_id = c->receipts[i].id;
      snprintf(e.text, sizeof(e.text),
               "No delivery confirmation after 120 seconds (not retried)");
      mesh_emit(c, &e);
      c->receipts[i].id = 0;
    }
  if (c->status.ready && mesh_idle(c) &&
      (uint32_t)(c->now - c->last_poll) >= 30000) {
    solar_mesh_ToRadio m = solar_mesh_ToRadio_init_zero;
    m.which_payload = solar_mesh_ToRadio_heartbeat_tag;
    m.payload.heartbeat.has_nonce = true;
    m.payload.heartbeat.nonce = ++c->nonce;
    transmit(c, &m);
    c->last_poll = c->now;
  }
}
static solar_mesh_result_t send(solar_mesh_client_t *c, unsigned channel,
                                const char *destination, const char *text,
                                uint32_t timestamp) {
  (void)timestamp;
  uint32_t dest = UINT32_MAX;
  if (destination && *destination) {
    if (strlen(destination) != 9 || destination[0] != '!')
      return SOLAR_MESH_ARGUMENT;
    dest = 0;
    for (unsigned i = 1; i < 9; ++i) {
      char ch = destination[i];
      unsigned v = ch >= '0' && ch <= '9'   ? ch - '0'
                   : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                   : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
                                            : 16;
      if (v > 15)
        return SOLAR_MESH_ARGUMENT;
      dest = (dest << 4) | v;
    }
    if (!dest || dest == UINT32_MAX || dest == c->self_number)
      return SOLAR_MESH_ARGUMENT;
  }
  int receipt = -1;
  if (dest != UINT32_MAX) {
    for (unsigned i = 0; i < 4; ++i)
      if (!c->receipts[i].id) {
        receipt = (int)i;
        break;
      }
    if (receipt < 0)
      return SOLAR_MESH_BUSY;
  }
  solar_mesh_ToRadio m = solar_mesh_ToRadio_init_zero;
  m.which_payload = solar_mesh_ToRadio_packet_tag;
  solar_mesh_Packet *p = &m.payload.packet;
  p->has_destination = true;
  p->destination = dest;
  p->has_channel = true;
  p->channel = channel;
  p->has_id = true;
  p->id = ++c->next_id;
  if (!p->id)
    p->id = ++c->next_id;
  p->has_want_ack = true;
  p->want_ack = dest != UINT32_MAX;
  p->has_decoded = true;
  p->decoded.has_port = true;
  p->decoded.port = 1;
  p->decoded.has_payload = true;
  p->decoded.payload.size = strlen(text);
  memcpy(p->decoded.payload.bytes, text, p->decoded.payload.size);
  if (!transmit(c, &m))
    return SOLAR_MESH_IO;
  if (receipt >= 0) {
    c->receipts[receipt].id = p->id;
    c->receipts[receipt].destination = dest;
    c->receipts[receipt].sent = c->now;
  }
  c->pending_id = p->id;
  c->pending_channel = channel;
  c->pending_direct = dest != UINT32_MAX;
  c->waiting = 2;
  ++c->status.messages_tx;
  mesh_tx_event(c, SOLAR_MESH_TX_QUEUED, p->id, text);
  return SOLAR_MESH_OK;
}
static solar_mesh_result_t refresh(solar_mesh_client_t *c) {
  ++c->nonce;
  if (!c->nonce)
    ++c->nonce;
  c->status.node_count = c->status.channel_count = 0;
  memset(c->channels, 0, sizeof(c->channels));
  start(c);
  return SOLAR_MESH_OK;
}
const struct solar_mesh_driver solar_mesh_meshtastic_driver = {
    "meshtastic", 233, start, receive, tick, send, refresh};
