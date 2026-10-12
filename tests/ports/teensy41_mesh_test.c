#include "mesh_bridge.h"
#include "mesh_internal.h"
#include "meshcore_companion.h"
#include "meshtastic_client.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint8_t data[4096];
  size_t used, limit;
  int fail;
} transport;
static int write_bytes(void *arg, const uint8_t *bytes, size_t n) {
  transport *t = arg;
  if (t->fail)
    return -1;
  if (n > t->limit)
    n = t->limit;
  assert(t->used + n <= sizeof(t->data));
  memcpy(t->data + t->used, bytes, n);
  t->used += n;
  return (int)n;
}
static void flush(solar_mesh_client_t *c, transport *t) {
  for (unsigned i = 0; i < 600 && c->tx_size; ++i)
    solar_mesh_tick(c, c->now + 1);
  assert(!c->tx_size);
  assert(t->used > 0);
}
static void feed(solar_mesh_client_t *c, const uint8_t *p, size_t n, bool mc) {
  uint8_t frame[516];
  size_t h = mc ? 3 : 4;
  frame[0] = mc ? '>' : 0x94;
  frame[1] = mc ? (uint8_t)n : 0xc3;
  frame[2] = mc ? (uint8_t)(n >> 8) : (uint8_t)(n >> 8);
  if (!mc)
    frame[3] = (uint8_t)n;
  memcpy(frame + h, p, n);
  for (size_t i = 0; i < h + n; ++i)
    solar_mesh_feed(c, frame + i, 1, c->now + 1);
}
static void mt_feed(solar_mesh_client_t *c, solar_mesh_FromRadio *m) {
  uint8_t p[512];
  pb_ostream_t out = pb_ostream_from_buffer(p, sizeof(p));
  assert(pb_encode(&out, solar_mesh_FromRadio_fields, m));
  feed(c, p, out.bytes_written, false);
}
static solar_mesh_client_t *init(transport *t, const char *name) {
  memset(t, 0, sizeof(*t));
  t->limit = 3;
  solar_mesh_client_t *c = calloc(1, solar_mesh_client_size());
  assert(c);
  assert(solar_mesh_init(c, name, write_bytes, t, 10, 42) == SOLAR_MESH_OK);
  assert(solar_mesh_send(c, 0, NULL, "hello", 0) == SOLAR_MESH_OFFLINE);
  flush(c, t);
  return c;
}
static void mt_ready(solar_mesh_client_t *c) {
  // Independent golden wire fixtures (official protobuf field numbers), not our
  // encoder.
  const uint8_t self[] = {0x1a, 0x02, 0x08, 0x7b};
  feed(c, self, sizeof(self), false);
  const uint8_t wrong[] = {0x38, 0x29};
  feed(c, wrong, sizeof(wrong), false);
  assert(!c->status.ready);
  const uint8_t right[] = {0x38, 0x2a};
  feed(c, right, sizeof(right), false);
  assert(c->status.ready);
  assert(!strcmp(c->status.node_id, "!0000007b"));
}
static void meshtastic(void) {
  transport t;
  solar_mesh_client_t *c = init(&t, "meshtastic");
  const uint8_t config[] = {0x94, 0xc3, 0, 2, 0x18, 42};
  assert(t.used == sizeof(config));
  assert(!memcmp(t.data, config, sizeof(config)));
  mt_ready(c);
  solar_mesh_FromRadio m = solar_mesh_FromRadio_init_zero;
  m.which_payload = solar_mesh_FromRadio_node_tag;
  m.payload.node.has_number = true;
  m.payload.node.number = 5;
  m.payload.node.has_user = true;
  m.payload.node.user.has_name = true;
  strcpy(m.payload.node.user.name, "Test node");
  mt_feed(c, &m);
  assert(c->status.node_count == 1 && !strcmp(c->nodes[0].id, "!00000005"));
  m = (solar_mesh_FromRadio)solar_mesh_FromRadio_init_zero;
  m.which_payload = solar_mesh_FromRadio_channel_tag;
  m.payload.channel.has_index = true;
  m.payload.channel.index = 0;
  m.payload.channel.has_role = true;
  m.payload.channel.role = 1;
  m.payload.channel.has_settings = true;
  m.payload.channel.settings.has_name = true;
  strcpy(m.payload.channel.settings.name, "LongFast");
  mt_feed(c, &m);
  assert(c->channels[0].known && c->channels[0].enabled);
  m = (solar_mesh_FromRadio)solar_mesh_FromRadio_init_zero;
  m.which_payload = solar_mesh_FromRadio_packet_tag;
  solar_mesh_Packet *p = &m.payload.packet;
  p->has_source = true;
  p->source = 5;
  p->has_destination = true;
  p->destination = UINT32_MAX;
  p->has_decoded = true;
  p->decoded.has_port = true;
  p->decoded.port = 1;
  p->decoded.has_payload = true;
  p->decoded.payload.size = 5;
  memcpy(p->decoded.payload.bytes, "hi\n\t!", 5);
  mt_feed(c, &m);
  solar_mesh_event_t e;
  uint64_t cursor = 0, lost = 0;
  bool found = false;
  while (solar_mesh_event(c, &cursor, &e, &lost))
    if (e.kind == SOLAR_MESH_MESSAGE) {
      found = true;
      assert(!e.has_snr && !e.has_rssi);
      assert(!strcmp(e.text, "hi  !"));
    }
  assert(found);
  p->has_snr = p->has_rssi = true;
  p->snr = -2.25f;
  p->rssi = -98;
  mt_feed(c, &m);
  assert(solar_mesh_event(c, &cursor, &e, &lost) && e.has_snr &&
         e.snr_q4 == -9 && e.rssi == -98);
  // A one-byte truncated protobuf is rejected without corrupting the
  // connection.
  const uint8_t bad[] = {0x12, 0xff};
  feed(c, bad, sizeof(bad), false);
  assert(c->status.malformed == 1);
  assert(solar_mesh_send(c, 0, "!bad", "hello", 0) == SOLAR_MESH_ARGUMENT);
  assert(solar_mesh_send(c, 8, NULL, "hello", 0) == SOLAR_MESH_ARGUMENT);
  assert(solar_mesh_send(c, 0, NULL, "bad\ntext", 0) == SOLAR_MESH_ARGUMENT);
  assert(solar_mesh_send(c, 0, "!00000005", "hello", 0) == SOLAR_MESH_OK);
  uint32_t id = c->pending_id;
  t.used = 0;
  flush(c, &t);
  solar_mesh_ToRadio sent = solar_mesh_ToRadio_init_zero;
  pb_istream_t in = pb_istream_from_buffer(t.data + 4, t.used - 4);
  assert(pb_decode(&in, solar_mesh_ToRadio_fields, &sent));
  assert(sent.payload.packet.destination == 5);
  assert(sent.payload.packet.want_ack && sent.payload.packet.id == id &&
         sent.payload.packet.decoded.payload.size == 5);
  assert(solar_mesh_send(c, 0, NULL, "second", 0) == SOLAR_MESH_BUSY);
  m = (solar_mesh_FromRadio)solar_mesh_FromRadio_init_zero;
  m.which_payload = solar_mesh_FromRadio_queue_tag;
  m.payload.queue.has_id = true;
  m.payload.queue.id = id + 1;
  mt_feed(c, &m);
  assert(c->waiting);
  m.payload.queue.id = id;
  m.payload.queue.has_result = true;
  m.payload.queue.result = 35;
  mt_feed(c, &m);
  assert(!c->waiting);
  // Remote routing ACK differs from the local radio's queue acceptance.
  m = (solar_mesh_FromRadio)solar_mesh_FromRadio_init_zero;
  m.which_payload = solar_mesh_FromRadio_packet_tag;
  p = &m.payload.packet;
  p->has_source = true;
  p->source = 5;
  p->has_destination = true;
  p->destination = 123;
  p->has_decoded = true;
  p->decoded.has_port = true;
  p->decoded.port = 5;
  p->decoded.has_request_id = true;
  p->decoded.request_id = id;
  p->decoded.has_payload = true;
  p->decoded.payload.size = 2;
  p->decoded.payload.bytes[0] = 0x18;
  p->decoded.payload.bytes[1] = 0; // explicit oneof error_reason=NONE
  mt_feed(c, &m);
  found = false;
  while (solar_mesh_event(c, &cursor, &e, &lost))
    if (e.kind == SOLAR_MESH_TX_DELIVERED) {
      found = true;
      assert(e.packet_id == id);
    }
  assert(found);
  // Independent consumers see the same retained events, and overrun is
  // explicit.
  uint64_t old = cursor;
  for (unsigned i = 0; i < 50; ++i)
    mt_feed(c, &m);
  assert(solar_mesh_event(c, &cursor, &e, &lost));
  assert(lost == 18);
  uint64_t other = old, other_lost = 0;
  solar_mesh_event_t second;
  assert(solar_mesh_event(c, &other, &second, &other_lost));
  assert(second.sequence == e.sequence && other_lost == lost);
  m = (solar_mesh_FromRadio)solar_mesh_FromRadio_init_zero;
  m.which_payload = solar_mesh_FromRadio_rebooted_tag;
  m.payload.rebooted = true;
  mt_feed(c, &m);
  assert(c->failed && !c->status.ready);
  free(c);
}
static void mc_ready(solar_mesh_client_t *c, transport *t) {
  uint8_t info[80] = {MC_RESP_DEVICE_INFO, 3, 50, 8};
  memcpy(info + 60, "v1.12", 5);
  feed(c, info, sizeof(info), true);
  t->used = 0;
  flush(c, t);
  assert(t->data[3] == MC_CMD_APP_START);
  uint8_t self[64] = {MC_RESP_SELF_INFO};
  memcpy(self + 58, "Teensy", 6);
  feed(c, self, sizeof(self), true);
  assert(c->status.ready);
  solar_mesh_tick(c, c->now + 1);
  t->used = 0;
  flush(c, t);
  assert(t->data[3] == MC_CMD_GET_CONTACTS);
  uint8_t end[5] = {MC_RESP_END_OF_CONTACTS};
  feed(c, end, sizeof(end), true);
  for (unsigned ch = 0; ch < 8; ++ch) {
    solar_mesh_tick(c, c->now + 1);
    t->used = 0;
    flush(c, t);
    assert(t->data[3] == MC_CMD_GET_CHANNEL && t->data[4] == ch);
    uint8_t channel[50] = {MC_RESP_CHANNEL_INFO};
    channel[1] = ch;
    memcpy(channel + 2, "Channel", 7);
    feed(c, channel, sizeof(channel), true);
  }
  solar_mesh_tick(c, c->now + 1);
  t->used = 0;
  flush(c, t);
  assert(t->data[3] == MC_CMD_SYNC_NEXT_MESSAGE);
  uint8_t done = MC_RESP_NO_MORE_MESSAGES;
  feed(c, &done, 1, true);
  assert(mesh_idle(c));
}
static void meshcore(void) {
  transport t;
  solar_mesh_client_t *c = init(&t, "meshcore");
  assert(t.data[3] == MC_CMD_DEVICE_QUERY);
  mc_ready(c, &t);
  assert(c->status.channel_count == 8);
  uint8_t v3[] = {17, 8, 0x9c, 0xfe, 0,   0xff, 0,   1,   0,
                  0,  0, 'B',  'o',  'b', ':',  ' ', 'h', 'i'};
  feed(c, v3, sizeof(v3), true);
  assert(c->status.messages_rx == 1);
  uint64_t cursor = 0, lost = 0;
  solar_mesh_event_t e;
  bool found = false;
  while (solar_mesh_event(c, &cursor, &e, &lost))
    if (e.kind == SOLAR_MESH_MESSAGE) {
      found = true;
      assert(e.has_snr && e.snr_q4 == 8 && !e.has_rssi);
      assert(!strcmp(e.text, "Bob: hi"));
    }
  assert(found);
  // Every truncated contact length must be rejected, including the old 144..147
  // bug.
  for (size_t n = 1; n < 148; ++n) {
    uint8_t *contact = calloc(1, n);
    contact[0] = MC_RESP_CONTACT;
    mc_event_t event;
    assert(!mc_parse(contact, n, &event));
    free(contact);
  }
  uint8_t contact[148] = {MC_RESP_CONTACT};
  memcpy(contact + 100, "Alice", 5);
  feed(c, contact, sizeof(contact), true);
  assert(c->status.node_count == 1 && !strcmp(c->nodes[0].name, "Alice"));
  assert(solar_mesh_send(c, 0, NULL, "hello", 123) == SOLAR_MESH_OK);
  t.used = 0;
  flush(c, &t);
  assert(t.data[3] == MC_CMD_SEND_CHANNEL_TXT_MSG);
  uint8_t ok = MC_RESP_OK;
  feed(c, &ok, 1, true);
  assert(mesh_idle(c));
  assert(solar_mesh_send(c, 0, "001122334455", "direct", 123) == SOLAR_MESH_OK);
  t.used = 0;
  flush(c, &t);
  assert(t.data[3] == MC_CMD_SEND_TXT_MSG);
  assert(t.data[10] == 0 && t.data[11] == 0x11);
  uint8_t accepted[10] = {MC_RESP_SENT, 0, 0x78, 0x56, 0x34, 0x12};
  feed(c, accepted, sizeof(accepted), true);
  assert(!c->waiting);
  uint8_t ack[9] = {MC_PUSH_SEND_CONFIRMED, 0x78, 0x56, 0x34, 0x12};
  feed(c, ack, sizeof(ack), true);
  uint8_t raw[] = {MC_PUSH_LOG_RX_DATA, 0xf8, 0x9c, 1, 2, 3};
  feed(c, raw, sizeof(raw), true);
  assert(c->status.rssi == -100 && c->status.snr_q4 == -8);
  free(c);
}
static void failures(void) {
  transport t;
  solar_mesh_client_t *c = init(&t, "meshtastic");
  solar_mesh_tick(c, 11000);
  assert(c->failed && c->status.timeouts == 1);
  free(c);
  c = init(&t, "meshcore");
  uint8_t incomplete[] = {'>', 20, 0, 1};
  solar_mesh_feed(c, incomplete, sizeof(incomplete), 100);
  solar_mesh_tick(c, 1200);
  assert(c->status.malformed == 1);
  uint8_t huge[] = {'>', 0xff, 0xff};
  solar_mesh_feed(c, huge, sizeof(huge), 1201);
  assert(c->status.malformed == 2);
  free(c);
  c = calloc(1, solar_mesh_client_size());
  memset(&t, 0, sizeof(t));
  assert(solar_mesh_init(c, "bogus", write_bytes, &t, 0, 1) ==
         SOLAR_MESH_UNSUPPORTED);
  assert(solar_mesh_init(c, "meshcore", write_bytes, &t, 0, 1) ==
         SOLAR_MESH_OK);
  solar_mesh_tick(c, 2001);
  assert(c->failed && c->status.timeouts == 1);
  free(c);
  // Deterministic random framed data, including truncated protobuf/contact
  // bodies.
  uint32_t random = 0x12345678;
  for (unsigned driver = 0; driver < 2; ++driver) {
    c = init(&t, driver ? "meshcore" : "meshtastic");
    if (driver)
      mc_ready(c, &t);
    else
      mt_ready(c);
    for (unsigned i = 0; i < 5000; ++i) {
      uint8_t p[255];
      random = random * 1664525 + 1013904223;
      size_t n = 1 + (random % 255);
      for (size_t j = 0; j < n; ++j) {
        random = random * 1664525 + 1013904223;
        p[j] = random >> 24;
      }
      // Reinitialize after intentional random reboot/error messages.
      if (c->failed) {
        free(c);
        c = init(&t, driver ? "meshcore" : "meshtastic");
      }
      feed(c, p, n, driver != 0);
    }
    free(c);
  }
}
static void bridge_test(void) {
  solar_mesh_bridge_t b = {0};
  b.channel = 2;
  solar_mesh_event_t e = {0};
  e.kind = SOLAR_MESH_MESSAGE;
  e.channel = 2;
  strcpy(e.text, "hello");
  char out[241];
  e.direct = true;
  assert(
      !solar_mesh_bridge_prepare(&b, &e, "meshcore", 160, 0, out, sizeof(out)));
  e.direct = false;
  e.channel = 1;
  assert(
      !solar_mesh_bridge_prepare(&b, &e, "meshcore", 160, 0, out, sizeof(out)));
  e.channel = 2;
  strcpy(e.text, "Alice: [bridge meshtastic] hello");
  assert(
      !solar_mesh_bridge_prepare(&b, &e, "meshcore", 160, 0, out, sizeof(out)));
  strcpy(e.text, "hello");
  assert(
      solar_mesh_bridge_prepare(&b, &e, "meshcore", 160, 0, out, sizeof(out)));
  assert(!strcmp(out, "[bridge meshcore] hello"));
  solar_mesh_bridge_complete(&b, &e, 0, SOLAR_MESH_OK);
  assert(!solar_mesh_bridge_prepare(&b, &e, "meshcore", 160, 11000, out,
                                    sizeof(out))); // duplicate
  strcpy(e.text, "second");
  assert(!solar_mesh_bridge_prepare(&b, &e, "meshcore", 160, 9000, out,
                                    sizeof(out))); // rate
  assert(solar_mesh_bridge_prepare(&b, &e, "meshcore", 160, 11000, out,
                                   sizeof(out)));
  solar_mesh_bridge_complete(&b, &e, 11000, SOLAR_MESH_BUSY);
  assert(b.forwarded == 1 && b.dropped == 3);
  memset(e.text, 'x', 160);
  e.text[160] = 0;
  assert(!solar_mesh_bridge_prepare(&b, &e, "meshcore", 160, 11000, out,
                                    sizeof(out)));
  strcpy(e.text, "hello");
  assert(solar_mesh_bridge_prepare(&b, &e, "meshcore", 160, 130000, out,
                                   sizeof(out)));
}
static void notified(void *arg) {
  solar_mesh_client_t *c = arg;
  assert(c->event_sequence > 0);
  const solar_mesh_event_t *e =
      &c->events[(c->event_sequence - 1) % SOLAR_MESH_EVENTS];
  assert(e->sequence ==
         c->event_sequence); // notification only after publishing
  ++c->pending_ack;
}
static void notification_test(void) {
  transport t;
  solar_mesh_client_t *c = init(&t, "meshtastic");
  solar_mesh_set_notify(c, notified, c);
  mt_ready(c);
  assert(c->pending_ack == 1);
  mesh_note(c, SOLAR_MESH_MESSAGE, "new incoming message");
  assert(c->pending_ack == 2);
  solar_mesh_set_notify(c, NULL, NULL);
  mesh_note(c, SOLAR_MESH_STATUS, "unsubscribed");
  assert(c->pending_ack == 2);
  free(c);
}
int main(void) {
  meshtastic();
  meshcore();
  failures();
  bridge_test();
  notification_test();
  printf("mesh host tests passed; client state %zu bytes\n",
         solar_mesh_client_size());
}
