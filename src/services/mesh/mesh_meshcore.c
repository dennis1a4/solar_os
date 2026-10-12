#include "mesh_internal.h"
#include "meshcore_companion.h"
#include <stdio.h>
#include <string.h>

enum { QUERY = 1, SELF, CONTACTS, CHANNEL, DRAIN, SEND, STATS };
static void hex(char *out, const uint8_t *in, unsigned n) {
  const char *digits = "0123456789abcdef";
  for (unsigned i = 0; i < n; ++i) {
    out[2 * i] = digits[in[i] >> 4];
    out[2 * i + 1] = digits[in[i] & 15];
  }
  out[n * 2] = 0;
}
static void command(solar_mesh_client_t *c, const uint8_t *p, size_t n,
                    unsigned wait) {
  c->tx_size = mc_frame_encode(p, n, c->tx, sizeof(c->tx));
  c->tx_used = 0;
  c->tx_started = c->now;
  c->response_started = c->now;
  c->waiting = wait;
  if (!c->tx_size)
    mesh_error(c, "Cannot encode radio command", true);
}
static void start(solar_mesh_client_t *c) {
  uint8_t p[64];
  size_t n = mc_cmd_device_query(p, sizeof(p), 3);
  command(c, p, n, QUERY);
  mesh_note(c, SOLAR_MESH_STATUS, "Querying MeshCore companion");
}
static solar_mesh_result_t refresh(solar_mesh_client_t *c) {
  c->refresh = true;
  c->phase = CONTACTS;
  c->next_channel = 0;
  c->status.node_count = c->status.channel_count = 0;
  memset(c->channels, 0, sizeof(c->channels));
  return SOLAR_MESH_OK;
}
static void receive(solar_mesh_client_t *c, const uint8_t *p, size_t n) {
  if (p[0] == MC_PUSH_LOG_RX_DATA) {
    if (n < 3) {
      ++c->status.malformed;
      return;
    }
    solar_mesh_event_t e = {0};
    e.kind = SOLAR_MESH_PACKET;
    e.has_snr = e.has_rssi = true;
    e.snr_q4 = (int8_t)p[1];
    e.rssi = (int8_t)p[2];
    c->status.has_snr = c->status.has_rssi = true;
    c->status.snr_q4 = e.snr_q4;
    c->status.rssi = e.rssi;
    snprintf(e.text, sizeof(e.text), "Raw radio packet (%u bytes)",
             (unsigned)n - 3);
    mesh_emit(c, &e);
    return;
  }
  mc_event_t m;
  if (!mc_parse(p, n, &m)) {
    ++c->status.malformed;
    return;
  }
  switch (m.code) {
  case MC_RESP_DEVICE_INFO:
    if (c->waiting == QUERY) {
      mesh_text(c->status.firmware, sizeof(c->status.firmware),
                m.u.device_info.ver, sizeof(m.u.device_info.ver));
      mesh_text(c->status.model, sizeof(c->status.model), m.u.device_info.model,
                sizeof(m.u.device_info.model));
      uint8_t b[64];
      size_t len = mc_cmd_app_start(b, sizeof(b), "SolarOS");
      command(c, b, len, SELF);
    }
    break;
  case MC_RESP_SELF_INFO:
    if (c->waiting == SELF) {
      c->waiting = 0;
      c->status.ready = true;
      hex(c->status.node_id, m.u.self_info.public_key, 32);
      mesh_text(c->status.name, sizeof(c->status.name), m.u.self_info.name,
                sizeof(m.u.self_info.name));
      mesh_note(c, SOLAR_MESH_STATUS, "MeshCore ready");
      refresh(c);
      c->drain = true;
    }
    break;
  case MC_RESP_CONTACTS_START:
    if (c->waiting == CONTACTS)
      c->response_started = c->now;
    break;
  case MC_RESP_CONTACT:
  case MC_PUSH_NEW_ADVERT: {
    char id[65];
    hex(id, m.u.contact.public_key, 32);
    mesh_node(c, id, m.u.contact.adv_name, m.u.contact.last_advert);
    if (c->waiting == CONTACTS)
      c->response_started = c->now;
    break;
  }
  case MC_RESP_END_OF_CONTACTS:
    if (c->waiting == CONTACTS) {
      c->waiting = 0;
      c->phase = CHANNEL;
    }
    break;
  case MC_RESP_CHANNEL_INFO: {
    unsigned i = m.u.channel_info.channel_idx;
    if (i < SOLAR_MESH_CHANNELS) {
      solar_mesh_channel_t *ch = &c->channels[i];
      if (!ch->known)
        ++c->status.channel_count;
      ch->known = true;
      ch->enabled = m.u.channel_info.name[0] != 0;
      mesh_text(ch->name, sizeof(ch->name), m.u.channel_info.name,
                sizeof(m.u.channel_info.name));
    }
    if (c->waiting == CHANNEL && i == c->next_channel) {
      c->waiting = 0;
      ++c->next_channel;
    }
    break;
  }
  case MC_PUSH_MSG_WAITING:
    c->drain = true;
    break;
  case MC_RESP_NO_MORE_MESSAGES:
    if (c->waiting == DRAIN) {
      c->waiting = 0;
      c->drain = false;
    }
    break;
  case MC_RESP_CHANNEL_MSG_RECV:
  case MC_RESP_CHANNEL_MSG_RECV_V3:
  case MC_RESP_CONTACT_MSG_RECV:
  case MC_RESP_CONTACT_MSG_RECV_V3: {
    solar_mesh_event_t e = {0};
    e.kind = SOLAR_MESH_MESSAGE;
    e.direct = m.code == MC_RESP_CONTACT_MSG_RECV ||
               m.code == MC_RESP_CONTACT_MSG_RECV_V3;
    if (e.direct) {
      hex(e.sender, m.u.contact_msg.pubkey_prefix, 6);
      e.timestamp = m.u.contact_msg.sender_ts;
      e.snr_q4 = m.u.contact_msg.snr_q4;
      mesh_text(e.text, sizeof(e.text), m.u.contact_msg.text,
                sizeof(m.u.contact_msg.text));
    } else {
      if (m.u.channel_msg.channel_idx < 0 ||
          m.u.channel_msg.channel_idx >= SOLAR_MESH_CHANNELS)
        break;
      e.channel = m.u.channel_msg.channel_idx;
      e.timestamp = m.u.channel_msg.sender_ts;
      e.snr_q4 = m.u.channel_msg.snr_q4;
      mesh_text(e.text, sizeof(e.text), m.u.channel_msg.text,
                sizeof(m.u.channel_msg.text));
    }
    e.has_snr = e.snr_q4 != MC_SNR_NONE; // V3 reserved bytes are NOT RSSI.
    if (e.has_snr) {
      c->status.has_snr = true;
      c->status.snr_q4 = e.snr_q4;
    }
    ++c->status.messages_rx;
    mesh_emit(c, &e);
    if (c->waiting == DRAIN)
      c->waiting = 0;
    break;
  }
  case MC_RESP_OK:
  case MC_RESP_SENT:
    if (c->waiting == SEND) {
      c->waiting = 0;
      if (m.code == MC_RESP_SENT)
        c->pending_ack = m.u.msg_sent.expected_ack;
      mesh_tx_event(c, SOLAR_MESH_TX_ACCEPTED, c->pending_id,
                    "Radio accepted message; delivery unconfirmed");
    }
    break;
  case MC_PUSH_SEND_CONFIRMED: {
    // ACK tags are wire identifiers. Do not associate an old ACK with a newer
    // send.
    solar_mesh_event_t e = {0};
    e.kind = SOLAR_MESH_TX_DELIVERED;
    e.packet_id = m.u.send_confirmed.ack_code;
    snprintf(e.text, sizeof(e.text), "Direct message ACK %08lx (%lu ms)",
             (unsigned long)e.packet_id,
             (unsigned long)m.u.send_confirmed.round_trip);
    mesh_emit(c, &e);
    break;
  }
  case MC_RESP_ERR: {
    unsigned waiting = c->waiting;
    c->waiting = 0;
    if (waiting == CHANNEL) {
      c->refresh = false;
      break;
    } // older radios have fewer channels
    char msg[96];
    snprintf(msg, sizeof(msg), "MeshCore error %d (command %u)", m.u.err_code,
             waiting);
    mesh_error(c, msg, waiting == QUERY || waiting == SELF);
    if (waiting == CONTACTS)
      c->refresh = false;
    if (waiting == DRAIN)
      c->drain = false;
    break;
  }
  default:
    break;
  }
}
static void tick(solar_mesh_client_t *c) {
  if (!mesh_idle(c) || !c->status.ready)
    return;
  uint8_t p[64];
  size_t n;
  if (c->refresh) {
    if (c->phase == CONTACTS) {
      n = mc_cmd_get_contacts(p, sizeof(p), 0);
      command(c, p, n, CONTACTS);
      return;
    }
    if (c->next_channel < SOLAR_MESH_CHANNELS) {
      n = mc_cmd_get_channel(p, sizeof(p), c->next_channel);
      command(c, p, n, CHANNEL);
      return;
    }
    c->refresh = false;
  }
  if (c->drain || (uint32_t)(c->now - c->last_poll) >= 5000) {
    c->drain = true;
    c->last_poll = c->now;
    n = mc_cmd_sync_next_message(p, sizeof(p));
    command(c, p, n, DRAIN);
  }
}
static solar_mesh_result_t send(solar_mesh_client_t *c, unsigned channel,
                                const char *destination, const char *text,
                                uint32_t timestamp) {
  uint8_t p[255];
  size_t n;
  if (destination && *destination) {
    size_t len = strlen(destination);
    if (len != 12 && len != 64)
      return SOLAR_MESH_ARGUMENT;
    uint8_t key[32] = {0};
    for (size_t i = 0; i < len; ++i) {
      char ch = destination[i];
      unsigned v = ch >= '0' && ch <= '9'   ? ch - '0'
                   : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                   : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
                                            : 16;
      if (v > 15)
        return SOLAR_MESH_ARGUMENT;
      key[i / 2] = (key[i / 2] << 4) | v;
    }
    n = mc_cmd_send_txt_msg(p, sizeof(p), MC_TXT_PLAIN, 0, timestamp, key, 6,
                            text);
  } else {
    char named[184];
    snprintf(named, sizeof(named), "%.20s: %s",
             c->status.name[0] ? c->status.name : "SolarOS", text);
    n = mc_cmd_send_channel_text(p, sizeof(p), MC_TXT_PLAIN, channel, timestamp,
                                 named);
  }
  if (!n)
    return SOLAR_MESH_ARGUMENT;
  command(c, p, n, SEND);
  c->pending_channel = channel;
  c->pending_direct = destination && *destination;
  c->pending_id = ++c->next_id;
  c->pending_ack = 0;
  ++c->status.messages_tx;
  mesh_tx_event(c, SOLAR_MESH_TX_QUEUED, c->pending_id, text);
  return SOLAR_MESH_OK;
}
const struct solar_mesh_driver solar_mesh_meshcore_driver = {
    "meshcore", 160, start, receive, tick, send, refresh};
