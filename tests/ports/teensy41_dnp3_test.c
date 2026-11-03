#include "solar_os_dnp3_lab.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Independent bitwise CRC oracle, rather than using the production encoder to
 * validate production output. Polynomial is reflected CRC-16/DNP. */
static uint16_t crc(const uint8_t *p, size_t n) {
  uint16_t c = 0;
  while (n--) {
    c ^= *p++;
    for (unsigned i = 0; i < 8; i++)
      c = (c >> 1) ^ ((c & 1) ? 0xa6bc : 0);
  }
  return ~c;
}
static size_t frame(uint8_t *p, unsigned ctl, unsigned src, unsigned dst,
                    const uint8_t *body, size_t n) {
  p[0] = 5;
  p[1] = 100;
  p[2] = n + 5;
  p[3] = ctl;
  p[4] = dst;
  p[5] = dst >> 8;
  p[6] = src;
  p[7] = src >> 8;
  uint16_t c = crc(p, 8);
  p[8] = c;
  p[9] = c >> 8;
  size_t at = 10;
  while (n) {
    size_t m = n > 16 ? 16 : n;
    memcpy(p + at, body, m);
    c = crc(body, m);
    at += m;
    p[at++] = c;
    p[at++] = c >> 8;
    body += m;
    n -= m;
  }
  return at;
}
static void valid(const uint8_t *p, size_t n) {
  assert(n >= 10 && p[0] == 5 && p[1] == 100);
  assert(crc(p, 8) == (p[8] | p[9] << 8));
  size_t left = p[2] - 5, at = 10;
  while (left) {
    size_t m = left > 16 ? 16 : left;
    assert(at + m + 2 <= n);
    assert(crc(p + at, m) == (p[at + m] | p[at + m + 1] << 8));
    left -= m;
    at += m + 2;
  }
  assert(at == n);
}
static dnp_lab_t *fresh(unsigned mode) {
  dnp_lab_config_t c;
  dnp_lab_defaults(&c);
  c.mode = mode;
  c.interval_ms = 0;
  dnp_lab_t *l = malloc(sizeof(*l));
  assert(l);
  dnp_lab_init(l, &c);
  return l;
}
static void cmd(dnp_lab_t *l, const char *s) {
  char out[128];
  bool ok = dnp_lab_command(l, s, false, out, sizeof(out));
  if (!ok)
    fprintf(stderr, "command %s: %s\n", s, out);
  assert(ok);
}
static size_t drain(dnp_lab_t *l, unsigned dir, uint8_t *out, uint32_t now) {
  size_t at = 0;
  const dnp_lab_packet_t *q;
  while ((q = dnp_lab_output(l, dir, now))) {
    size_t n = q->length - q->offset;
    if (n > 3)
      n = 3;
    memcpy(out + at, q->bytes + q->offset, n);
    at += n;
    dnp_lab_sent(l, dir, n);
  }
  return at;
}
static void pump(dnp_lab_t *l, uint32_t now) {
  for (unsigned i = 0; i < 100; i++) {
    dnp_lab_step(l, now);
    bool work = false;
    for (unsigned dir = 0; dir < 2; dir++) {
      const dnp_lab_packet_t *q = dnp_lab_output(l, dir, now);
      if (q) {
        uint8_t copy[292];
        size_t n = q->length - q->offset;
        memcpy(copy, q->bytes + q->offset, n);
        dnp_lab_sent(l, dir, n);
        assert(dnp_lab_feed(l, dir, copy, n, now));
        work = true;
      }
    }
    if (!work && !l->direction[0].used && !l->direction[1].used)
      return;
  }
  assert(!"loopback did not settle");
}
static void encoding(void) {
  assert(dnp3_crc((const uint8_t *)"123456789", 9) == 0xea82);
  uint8_t body[250], wire[292], other[292], unpacked[250];
  for (unsigned i = 0; i < 250; i++)
    body[i] = i;
  for (size_t n = 0; n <= 250; n++) {
    size_t m = dnp_lab_frame(wire, sizeof(wire), 0xc4, 1, 1024, body, n);
    assert(m == frame(other, 0xc4, 1, 1024, body, n));
    assert(!memcmp(wire, other, m));
    valid(wire, m);
    assert(dnp_lab_unpack(wire, m, unpacked) == (int)n);
    assert(!memcmp(body, unpacked, n));
    assert(dnp_lab_unpack(wire, m - 1, unpacked) == -1);
    wire[8] ^= 1;
    assert(dnp_lab_unpack(wire, m, unpacked) == -1);
  }
}
static void transparent(void) {
  dnp_lab_t *l = fresh(0);
  uint8_t input[4096], output[8192];
  for (unsigned i = 0; i < 4096; i++)
    input[i] = i * 37;
  assert(dnp_lab_feed(l, 0, input, sizeof(input), 1));
  assert(!dnp_lab_feed(l, 0, input, 1, 1));
  size_t at = 0;
  for (unsigned i = 0; i < 40; i++) {
    dnp_lab_step(l, i + 1);
    at += drain(l, 0, output + at, i + 1);
  }
  assert(at == sizeof(input) && !memcmp(input, output, at));
  assert(!l->stats[1].tx);
  free(l);
}
static void rewrites(void) {
  uint8_t wire[292], out[4096], body[250];
  dnp_lab_t *l = fresh(0);
  cmd(l, "rule add binary b2a 4 1 3");
  cmd(l, "rule add analog b2a 4 -1234 5");
  cmd(l, "rule add link b2a src 1024 77");
  uint8_t response[] = {0xc0, 0xc0, 129, 0, 0, 1, 2, 0, 4, 4, 1,
                        30,   1,    0,   4, 4, 1, 0, 0, 0, 0};
  size_t n = frame(wire, 0x44, 1024, 1, response, sizeof(response));
  assert(dnp_lab_feed(l, 1, wire, n, 1));
  dnp_lab_step(l, 1);
  size_t m = drain(l, 1, out, 1);
  assert(m == n);
  valid(out, m);
  assert(out[6] == 77);
  assert(dnp_lab_unpack(out, m, body) == sizeof(response));
  assert(body[10] == 0x83 && body[16] == 5);
  assert((int32_t)((uint32_t)body[17] | (uint32_t)body[18] << 8 |
                   (uint32_t)body[19] << 16 | (uint32_t)body[20] << 24) ==
         -1234);
  assert(l->stats[1].changed == 1);
  free(l);
  l = fresh(0);
  cmd(l, "rule add control a2b 4 2000");
  uint8_t command[] = {0xc0, 0xc0, 5, 12, 1, 0x28, 1, 0, 4, 0, 3,
                       1,    0,    0, 0,  0, 0,    0, 0, 0, 0};
  n = frame(wire, 0xc4, 1, 1024, command, sizeof(command));
  dnp_lab_feed(l, 0, wire, n, 1);
  dnp_lab_step(l, 1);
  m = drain(l, 0, out, 1);
  valid(out, m);
  dnp_lab_unpack(out, m, body);
  assert(body[8] == 0xd0 && body[9] == 7);
  free(l);
  /* A supported object followed by unknown data must roll back edits/hits. */
  l = fresh(0);
  cmd(l, "rule add binary b2a 4 1");
  response[11] = 250;
  n = frame(wire, 0x44, 1024, 1, response, sizeof(response));
  dnp_lab_feed(l, 1, wire, n, 1);
  dnp_lab_step(l, 1);
  m = drain(l, 1, out, 1);
  assert(m == n && !memcmp(wire, out, n));
  assert(!l->config.rules[0].hits && l->stats[1].skipped == 1);
  free(l);
  /* Never hold an initial transport fragment waiting for an ACK-gated next. */
  l = fresh(0);
  cmd(l, "rule add binary b2a 4 1");
  response[0] = 0x80;
  n = frame(wire, 0x44, 1024, 1, response, sizeof(response));
  dnp_lab_feed(l, 1, wire, n, 1);
  dnp_lab_step(l, 1);
  m = drain(l, 1, out, 1);
  assert(m == n && !memcmp(wire, out, n));
  free(l);
}
static void faults(void) {
  uint8_t wire[292], out[4096], payload[] = {0xc0, 0xc0, 1, 60, 1, 6};
  size_t n = frame(wire, 0xc4, 1, 1024, payload, sizeof(payload));
  dnp_lab_t *l = fresh(0);
  cmd(l, "fault a2b duplicate 1");
  cmd(l, "fault a2b delay 20");
  dnp_lab_feed(l, 0, wire, n, 100);
  dnp_lab_step(l, 100);
  assert(!dnp_lab_output(l, 0, 119));
  assert(drain(l, 0, out, 120) == 2 * n);
  assert(!memcmp(out, wire, n) && !memcmp(out + n, wire, n));
  cmd(l, "fault clear");
  cmd(l, "fault a2b crc 1");
  dnp_lab_feed(l, 0, wire, n, 200);
  dnp_lab_step(l, 200);
  assert(drain(l, 0, out, 200) == n && out[8] == (wire[8] ^ 1));
  cmd(l, "fault clear");
  cmd(l, "fault a2b drop 1");
  dnp_lab_feed(l, 0, wire, n, 300);
  dnp_lab_step(l, 300);
  assert(!drain(l, 0, out, 300));
  cmd(l, "fault clear");
  cmd(l, "fault a2b truncate 3 1");
  dnp_lab_feed(l, 0, wire, n, 400);
  dnp_lab_step(l, 400);
  assert(drain(l, 0, out, 400) == n - 3);
  free(l);
}
static void simulator(void) {
  dnp_lab_t *l = fresh(3);
  cmd(l, "points 32");
  cmd(l, "point bi 3 1 3");
  cmd(l, "point ai 3 -123456 5");
  cmd(l, "point bo 3 0");
  cmd(l, "point ao 3 456789");
  assert(dnp_lab_poll(l));
  pump(l, 1);
  assert(l->responses == 1 && !l->waiting);
  assert(l->master_points[3].binary == 1 && l->master_points[3].flags == 3 &&
         l->master_points[3].analog == -123456 &&
         l->master_points[3].analog_flags == 5);
  assert(l->master_points[3].binary_output == 0 &&
         l->master_points[3].analog_output == 456789);
  assert(dnp_lab_control(l, false, 3, 1, false));
  pump(l, 10);
  assert(l->points[3].binary_output && l->controls == 1 && !l->waiting &&
         !l->errors);
  assert(dnp_lab_control(l, true, 4, -98765, true));
  pump(l, 20);
  assert(l->points[4].analog_output == -98765 && l->controls == 2);
  assert(dnp_lab_control(l, false, 60, 1, false));
  pump(l, 30);
  assert(!l->waiting && l->controls == 2 && l->errors);
  assert(dnp_lab_poll(l));
  dnp_lab_step(l, 3000);
  assert(l->timeouts == 1);
  free(l);
}
static void malformed(void) {
  uint8_t wire[292], out[20000], body[250];
  dnp_lab_t *l = fresh(0);
  cmd(l, "rule add analog b2a 1 100");
  uint32_t rng = 123;
  for (unsigned k = 0; k < 2000; k++) {
    size_t n = k % 250 + 1;
    for (unsigned i = 0; i < n; i++) {
      rng = rng * 1664525 + 1013904223;
      body[i] = rng >> 24;
    }
    body[0] = 0xc0;
    if (n >= 5) {
      body[1] = 0xc0;
      body[2] = 129;
    }
    size_t m = frame(wire, 0x44, 1024, 1, body, n);
    assert(dnp_lab_feed(l, 1, wire, m, k));
    dnp_lab_step(l, k);
    size_t got = drain(l, 1, out, k);
    assert(got == m);
    valid(out, got);
  }
  free(l);
}
static void protocol_edges(void) {
  uint8_t wire[292], out[4096], body[250];
  dnp_lab_t *l = fresh(DNP_LAB_OUTSTATION);
  uint8_t operate[] = {0xc0, 0xc0, 4, 12, 1, 0x28, 1, 0, 3, 0, 3,
                       1,    100,  0, 0,  0, 100,  0, 0, 0, 0};
  size_t n = frame(wire, 0xc4, 1, 1024, operate, sizeof(operate));
  dnp_lab_feed(l, 0, wire, n, 1);
  dnp_lab_step(l, 1);
  size_t m = drain(l, 1, out, 1);
  valid(out, m);
  int count = dnp_lab_unpack(out, m, body);
  assert(body[count - 1] == 2 && !l->controls); /* no select */
  operate[1] = 0xc1;
  operate[2] = 3;
  n = frame(wire, 0xc4, 1, 1024, operate, sizeof(operate));
  dnp_lab_feed(l, 0, wire, n, 2);
  dnp_lab_step(l, 2);
  drain(l, 1, out, 2);
  assert(l->selected);
  operate[1] = 0xc2;
  operate[2] = 4;
  n = frame(wire, 0xc4, 1, 1024, operate, sizeof(operate));
  dnp_lab_feed(l, 0, wire, n, 3000);
  dnp_lab_step(l, 3000);
  m = drain(l, 1, out, 3000);
  count = dnp_lab_unpack(out, m, body);
  assert(body[count - 1] == 2 && !l->controls); /* expired select */
  operate[1] = 0xc3;
  operate[2] = 5;
  n = frame(wire, 0xc4, 1, 1024, operate, sizeof(operate));
  dnp_lab_feed(l, 0, wire, n, 3100);
  dnp_lab_step(l, 3100);
  drain(l, 1, out, 3100);
  assert(l->controls == 1);
  /* Same application sequence/payload, different transport seq: retry cache. */
  operate[0] = 0xc1;
  n = frame(wire, 0xc4, 1, 1024, operate, sizeof(operate));
  dnp_lab_feed(l, 0, wire, n, 3101);
  dnp_lab_step(l, 3101);
  assert(drain(l, 1, out, 3101) > 0 && l->controls == 1);
  operate[1] = 0xc4;
  operate[0] = 0xc2;
  n = frame(wire, 0xf3, 1, 1024, operate, sizeof(operate));
  dnp_lab_feed(l, 0, wire, n, 3102);
  dnp_lab_step(l, 3102);
  assert(drain(l, 1, out, 3102) > 10 && l->controls == 2);
  operate[1] = 0xc5;
  operate[0] = 0xc3;
  n = frame(wire, 0xf3, 1, 1024, operate, sizeof(operate));
  dnp_lab_feed(l, 0, wire, n, 3103);
  dnp_lab_step(l, 3103);
  assert(drain(l, 1, out, 3103) == 10 && l->controls == 2); /* duplicate FCB */
  n = frame(wire, 0xc4, 1, 65535, operate, sizeof(operate));
  dnp_lab_feed(l, 0, wire, n, 3104);
  dnp_lab_step(l, 3104);
  assert(!drain(l, 1, out, 3104) && l->controls == 2);
  free(l);
  l = fresh(DNP_LAB_BRIDGE);
  cmd(l, "rule add binary b2a 9 1");
  uint8_t packed[] = {0xc0, 0xc0, 129, 0, 0, 1, 1, 0, 8, 17, 0, 0};
  n = frame(wire, 0x44, 1024, 1, packed, sizeof(packed));
  dnp_lab_feed(l, 1, wire, n, 1);
  dnp_lab_step(l, 1);
  m = drain(l, 1, out, 1);
  valid(out, m);
  dnp_lab_unpack(out, m, body);
  assert(body[10] == 2 && body[11] == 0);
  free(l);
  l = fresh(DNP_LAB_BRIDGE);
  cmd(l, "rule add analog b2a 4 12.5");
  uint8_t analog[] = {0xc0, 0xc0, 129, 0, 0, 30, 5, 0, 4, 4, 1, 0, 0, 0, 0};
  n = frame(wire, 0x44, 1024, 1, analog, sizeof(analog));
  dnp_lab_feed(l, 1, wire, n, 1);
  dnp_lab_step(l, 1);
  m = drain(l, 1, out, 1);
  valid(out, m);
  dnp_lab_unpack(out, m, body);
  assert(body[11] == 0 && body[12] == 0 && body[13] == 0x48 &&
         body[14] == 0x41);
  free(l);
  l = fresh(DNP_LAB_BRIDGE);
  cmd(l, "fault b2a flip 12 0x80 1");
  n = frame(wire, 0x44, 1024, 1, analog, sizeof(analog));
  dnp_lab_feed(l, 1, wire, n, 1);
  dnp_lab_step(l, 1);
  m = drain(l, 1, out, 1);
  assert(m == n && out[12] == (wire[12] ^ 128) &&
         dnp_lab_unpack(out, m, body) == -1);
  free(l);
}
static void stalled_receiver(void) {
  dnp_lab_t *l = fresh(DNP_LAB_BRIDGE);
  uint8_t input[24000], output[24000];
  size_t received = 0, sent = 0;
  for (unsigned i = 0; i < sizeof(input); i++)
    input[i] = (i * 73) ^ 0xa5;
  for (unsigned i = 0; i < 3000 && sent < sizeof(input); i++) {
    size_t part = sizeof(input) - received;
    if (part > 193)
      part = 193;
    if (part > dnp_lab_receive_space(l, 0))
      part = dnp_lab_receive_space(l, 0);
    if (part) {
      assert(dnp_lab_feed(l, 0, input + received, part, i));
      received += part;
    }
    dnp_lab_step(l, i);
    /* Block all writes long enough to fill the output queue and input buffer.
     */
    if (i > 200)
      sent += drain(l, 0, output + sent, i);
  }
  assert(received == sizeof(input) && sent == sizeof(input) &&
         !memcmp(input, output, sent));
  assert(l->stats[0].backpressure > 0 && !l->direction[0].count &&
         !l->direction[0].used);
  free(l);
}
int main(void) {
  stalled_receiver();
  protocol_edges();
  encoding();
  transparent();
  rewrites();
  faults();
  simulator();
  malformed();
  puts("DNP3 core: CRC oracle, transparency, partial writes, edits/rollback, "
       "faults, 32-point fragmented simulation, controls/timeouts, malformed "
       "objects passed");
}
