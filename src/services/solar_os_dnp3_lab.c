#include "solar_os_dnp3_lab.h"
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t le(const uint8_t *p, unsigned n) {
  uint32_t v = 0;
  for (unsigned i = 0; i < n; i++)
    v |= (uint32_t)p[i] << (8 * i);
  return v;
}
static void put(uint8_t *p, uint32_t v, unsigned n) {
  for (unsigned i = 0; i < n; i++)
    p[i] = v >> (8 * i);
}
void dnp_lab_log(dnp_lab_t *l, unsigned dir, const char *stage,
                 const char *text, const uint8_t *data, size_t n) {
  dnp_lab_event_t *e = &l->history[l->events % DNP_LAB_HISTORY];
  memset(e, 0, sizeof(*e));
  e->sequence = ++l->events;
  e->at = l->now;
  e->direction = dir;
  snprintf(e->stage, sizeof(e->stage), "%s", stage);
  snprintf(e->text, sizeof(e->text), "%s", text);
  if (n > sizeof(e->data))
    n = sizeof(e->data);
  e->length = n;
  if (n)
    memcpy(e->data, data, n);
}
static void observed(void *user, const char *text, const uint8_t *p, size_t n) {
  /* Named layout avoids retaining any callback-local data. */
  dnp_lab_observer_t *c = user;
  dnp_lab_log(c->lab, c->direction, p ? "RX" : "DECODE", text, p, n);
}
void dnp_lab_defaults(dnp_lab_config_t *c) {
  memset(c, 0, sizeof(*c));
  c->master = 1;
  c->outstation = 1024;
  c->interval_ms = 1000;
  c->timeout_ms = 2000;
  c->points = 16;
  for (unsigned i = 0; i < 2; i++) {
    c->endpoint[i].slot = i;
    c->endpoint[i].baud = 9600;
    c->endpoint[i].port = 20000;
    strcpy(c->endpoint[i].host, "127.0.0.1");
  }
}
void dnp_lab_init(dnp_lab_t *l, const dnp_lab_config_t *c) {
  memset(l, 0, sizeof(*l));
  l->config = *c;
  for (unsigned i = 0; i < DNP_LAB_POINTS; i++) {
    l->points[i].flags = 1;
    l->points[i].analog_flags = 1;
    l->points[i].output_flags = 1;
    l->points[i].analog_output_flags = 1;
  }
  for (unsigned i = 0; i < 2; i++) {
    l->observer[i].lab = l;
    l->observer[i].direction = i;
    dnp3_init(&l->direction[i].decode, observed, &l->observer[i]);
  }
}
static bool enqueue(dnp_lab_t *l, unsigned dir, const uint8_t *p, size_t n,
                    uint32_t due) {
  dnp_lab_direction_t *d = &l->direction[dir];
  if (!n)
    return true;
  if (n > DNP3_FRAME_MAX || d->count == DNP_LAB_QUEUE)
    return false;
  dnp_lab_packet_t *q = &d->queue[(d->head + d->count) % DNP_LAB_QUEUE];
  memcpy(q->bytes, p, n);
  q->length = n;
  q->offset = 0;
  q->due = due;
  d->count++;
  return true;
}
const dnp_lab_packet_t *dnp_lab_output(dnp_lab_t *l, unsigned dir,
                                       uint32_t now) {
  if (dir > 1)
    return NULL;
  dnp_lab_direction_t *d = &l->direction[dir];
  if (!d->count || (int32_t)(now - d->queue[d->head].due) < 0)
    return NULL;
  return &d->queue[d->head];
}
void dnp_lab_sent(dnp_lab_t *l, unsigned dir, size_t n) {
  dnp_lab_direction_t *d = &l->direction[dir];
  if (!d->count)
    return;
  dnp_lab_packet_t *q = &d->queue[d->head];
  if (n > (size_t)(q->length - q->offset)) {
    l->errors++;
    return;
  }
  q->offset += n;
  l->stats[dir].tx += n;
  if (q->offset == q->length) {
    dnp_lab_log(l, dir, "TX", "forwarded / generated", q->bytes, q->length);
    d->head = (d->head + 1) % DNP_LAB_QUEUE;
    d->count--;
  }
}
size_t dnp_lab_receive_space(const dnp_lab_t *l, unsigned dir) {
  return dir < 2 ? DNP_LAB_RX - l->direction[dir].used : 0;
}
bool dnp_lab_feed(dnp_lab_t *l, unsigned dir, const uint8_t *p, size_t n,
                  uint32_t now) {
  if (dir > 1 || n > dnp_lab_receive_space(l, dir))
    return false;
  l->now = now;
  dnp_lab_direction_t *d = &l->direction[dir];
  memcpy(d->rx + d->used, p, n);
  d->used += n;
  d->touched = now;
  l->stats[dir].rx += n;
  dnp3_feed(&d->decode, p, n, now);
  return true;
}
void dnp_lab_gap(dnp_lab_t *l, unsigned dir) {
  l->assembly[dir].active = false;
  dnp3_gap(&l->direction[dir].decode);
  l->direction[dir].used = 0;
}
/* Mutation is transactional: a complete supported object walk succeeds before
 * any edited application is emitted. Unknown/malformed payloads remain
 * original. */
typedef struct {
  unsigned size, width, bits;
  bool flags, binary, analog, control, floating;
} layout_t;
static bool layout(unsigned g, unsigned v, layout_t *x) {
  memset(x, 0, sizeof(*x));
  if ((g == 1 || g == 10) && v == 1) {
    x->bits = 1;
    x->binary = true;
    return true;
  }
  if (g == 3 && v == 1) {
    x->bits = 2;
    return true;
  }
  if ((g == 1 || g == 3 || g == 10) && v == 2) {
    x->size = 1;
    x->flags = true;
    x->binary = g != 3;
    return true;
  }
  if ((g == 2 || g == 4) && v >= 1 && v <= 3) {
    x->size = v == 1 ? 1 : v == 2 ? 7 : 3;
    x->flags = true;
    x->binary = g == 2;
    return true;
  }
  if (g == 11 && (v == 1 || v == 2)) {
    x->size = v == 1 ? 1 : 7;
    x->binary = x->flags = true;
    return true;
  }
  if (g == 12 && v == 1) {
    x->size = 11;
    x->control = true;
    return true;
  }
  if (g == 30 && v >= 1 && v <= 6) {
    x->width = v == 2 || v == 4 ? 2 : v == 6 ? 8 : 4;
    x->flags = v != 3 && v != 4;
    x->floating = v >= 5;
    x->analog = true;
  } else if (g == 32 && v >= 1 && v <= 8) {
    x->width = v == 2 || v == 4 ? 2 : v == 6 || v == 8 ? 8 : 4;
    x->flags = true;
    x->floating = v >= 5;
    x->analog = true;
    x->size = (v == 3 || v == 4 || v == 7 || v == 8) ? 6 : 0;
  } else if (g == 40 && v >= 1 && v <= 4) {
    x->width = v == 2 ? 2 : v == 4 ? 8 : 4;
    x->flags = x->analog = true;
    x->floating = v >= 3;
  } else if (g == 41 && v >= 1 && v <= 4) {
    x->width = v == 2 ? 2 : v == 4 ? 8 : 4;
    x->floating = v >= 3;
    x->control = true;
    x->size = 1;
  } else if ((g == 20 || g == 22 || g == 23) &&
             (v == 1 || v == 2 || v == 5 || v == 6)) {
    x->width = (v == 1 || v == 5) ? 4 : 2;
    x->flags = g != 20 || v < 5;
    x->size = g != 20 && v >= 5 ? 6 : 0;
  } else if ((g == 50 && v == 1) || (g == 51 && (v == 1 || v == 2))) {
    x->size = 6;
    return true;
  } else if (g == 52 && (v == 1 || v == 2)) {
    x->size = 2;
    return true;
  } else if ((g == 110 || g == 111) && v) {
    x->size = v;
    return true;
  } else
    return false;
  x->size += x->width + x->flags;
  return true;
}
static bool set_analog(uint8_t *p, const layout_t *x, double value) {
  if (!isfinite(value))
    return false;
  p += x->flags;
  if (x->floating) {
    if (x->width == 4) {
      if (fabs(value) > FLT_MAX)
        return false;
      float f = (float)value;
      uint32_t v;
      memcpy(&v, &f, 4);
      put(p, v, 4);
    } else {
      uint64_t v;
      memcpy(&v, &value, 8);
      for (unsigned k = 0; k < 8; k++)
        p[k] = v >> (8 * k);
    }
  } else {
    if (value < INT32_MIN || value > INT32_MAX ||
        (x->width == 2 && (value < INT16_MIN || value > INT16_MAX)))
      return false;
    put(p, (uint32_t)(int32_t)value, x->width);
  }
  return true;
}
static bool objects(dnp_lab_t *l, unsigned dir, uint8_t *p, size_t n,
                    unsigned mode, bool *changed) {
  while (n) {
    if (n < 3)
      return false;
    unsigned g = p[0], v = p[1], q = p[2], prefix = q >> 4, range = q & 15;
    p += 3;
    n -= 3;
    if ((q & 128) || prefix > 3)
      return false;
    uint32_t first = 0, count = 0;
    unsigned iw = prefix ? 1U << (prefix - 1) : 0;
    uint8_t *range_start = p;
    unsigned rw = 0;
    if (range <= 2) {
      rw = 1U << range;
      if (prefix || n < 2 * rw)
        return false;
      first = le(p, rw);
      uint32_t last = le(p + rw, rw);
      if (last < first || last - first >= 65536)
        return false;
      count = last - first + 1;
      p += 2 * rw;
      n -= 2 * rw;
    } else if (range >= 7 && range <= 9) {
      unsigned w = 1U << (range - 7);
      if (n < w)
        return false;
      count = le(p, w);
      p += w;
      n -= w;
    } else
      return false;
    layout_t x;
    if (!layout(g, v, &x))
      return false;
    size_t needed = x.bits ? ((size_t)count * x.bits + 7) / 8
                           : (size_t)count * (x.size + iw);
    if (count > 65536 || needed > n || (x.bits && prefix))
      return false;
    for (uint32_t i = 0; i < count; i++) {
      uint8_t *index_ptr = p;
      uint32_t index = iw ? le(p, iw) : first + i;
      uint8_t *value = p + iw;
      for (unsigned r = 0; r < DNP_LAB_RULES; r++) {
        dnp_lab_rule_t *rule = &l->config.rules[r];
        if (mode != 1 || !rule->enabled || rule->direction != dir ||
            rule->index != index)
          continue;
        if (rule->kind == DNP_LAB_CONTROL_INDEX && x.control) {
          unsigned w = iw ? iw : rw;
          bool fit =
              w && (w >= 2 || rule->replacement <= 255) && (iw || count == 1);
          if (!fit) {
            rule->skipped++;
            continue;
          }
          if (iw)
            put(index_ptr, rule->replacement, w);
          else {
            put(range_start, rule->replacement, w);
            put(range_start + w, rule->replacement, w);
          }
          rule->hits++;
          *changed = true;
        } else if (rule->kind == DNP_LAB_BINARY && x.binary) {
          if (x.bits) {
            unsigned bit = i * x.bits;
            uint8_t mask = 1U << (bit % 8);
            if (rule->value)
              p[bit / 8] |= mask;
            else
              p[bit / 8] &= ~mask;
          } else {
            value[0] = (rule->flags >= 0 ? rule->flags : value[0]) & 0x7f;
            if (rule->value)
              value[0] |= 128;
          }
          rule->hits++;
          *changed = true;
        } else if (rule->kind == DNP_LAB_ANALOG && x.analog) {
          if (!set_analog(value, &x, rule->value)) {
            rule->skipped++;
            continue;
          }
          if (x.flags && rule->flags >= 0)
            value[0] = rule->flags;
          rule->hits++;
          *changed = true;
        }
      }
      if (mode == 0 && index < DNP_LAB_POINTS) {
        dnp_lab_point_t *point = &l->master_points[index];
        if (x.binary) {
          uint8_t state = x.bits ? !!(p[i / 8] & (1U << (i % 8)))
                                 : !!(value[0] & 128),
                  flags = x.bits ? 1 : value[0] & 127;
          if (g == 10 || g == 11) {
            point->binary_output = state;
            point->output_flags = flags;
          } else {
            point->binary = state;
            point->flags = flags;
          }
        }
        if (x.analog) {
          const uint8_t *num = value + x.flags;
          double number;
          if (x.floating) {
            if (x.width == 4) {
              uint32_t u = le(num, 4);
              float f;
              memcpy(&f, &u, 4);
              number = f;
            } else {
              uint64_t u = le(num, 4) | ((uint64_t)le(num + 4, 4) << 32);
              memcpy(&number, &u, 8);
            }
          } else
            number = x.width == 2 ? (int16_t)le(num, 2) : (int32_t)le(num, 4);
          if (isfinite(number) && number >= INT32_MIN && number <= INT32_MAX) {
            if (g == 40)
              point->analog_output = (int32_t)number;
            else
              point->analog = (int32_t)number;
          }
          if (g == 40)
            point->analog_output_flags = x.flags ? value[0] : 1;
          else
            point->analog_flags = x.flags ? value[0] : 1;
        }
      }
      if (!x.bits)
        p += iw + x.size;
    }
    if (x.bits)
      p += needed;
    n -= needed;
  }
  return true;
}
static bool active(dnp_lab_t *l, unsigned dir) {
  for (unsigned i = 0; i < DNP_LAB_RULES; i++)
    if (l->config.rules[i].enabled && l->config.rules[i].direction == dir)
      return true;
  dnp_lab_fault_t *f = &l->config.faults[dir];
  return f->drop_every || f->duplicate_every || f->crc_every ||
         f->truncate_every || f->flip_every || f->delay_ms;
}
static bool nth(uint32_t count, uint32_t every) {
  return every && count % every == 0;
}
static void transform(dnp_lab_t *l, unsigned dir, uint8_t *wire, size_t n) {
  uint8_t body[250], edited[250];
  int length = dnp_lab_unpack(wire, n, body);
  bool changed = false;
  uint16_t src = le(wire + 6, 2), dst = le(wire + 4, 2);
  if (length >= 0) {
    bool app_rules = false;
    for (unsigned i = 0; i < DNP_LAB_RULES; i++) {
      dnp_lab_rule_t *r = &l->config.rules[i];
      if (!r->enabled || r->direction != dir)
        continue;
      uint16_t *address = r->kind == DNP_LAB_LINK_SRC   ? &src
                          : r->kind == DNP_LAB_LINK_DST ? &dst
                                                        : NULL;
      if (address && *address == r->index) {
        *address = r->replacement;
        r->hits++;
        changed = true;
      }
      if (r->kind >= DNP_LAB_CONTROL_INDEX)
        app_rules = true;
    }
    if (app_rules) {
      /* Keep confirmed-link forwarding live: never hold earlier fragments
       * waiting for an ACK-gated successor just to edit a later object. */
      bool ok =
          length >= 3 && (body[0] & 0xc0) == 0xc0 && (body[1] & 0xc0) == 0xc0;
      unsigned header = body[2] == 129 || body[2] == 130 ? 5 : 3;
      memcpy(edited, body, length);
      bool app_changed = false;
      if (ok &&
          !((body[2] >= 2 && body[2] <= 6) || body[2] == 129 || body[2] == 130))
        ok = false;
      if (ok)
        ok = (unsigned)length >= header &&
             objects(l, dir, edited + header, length - header, 2, &app_changed);
      if (ok)
        ok = objects(l, dir, edited + header, length - header, 1, &app_changed);
      if (ok && app_changed) {
        memcpy(body, edited, length);
        changed = true;
      } else if (!ok) {
        l->stats[dir].skipped++;
        dnp_lab_log(l, dir, "SKIP",
                    "application edit skipped: fragmented/unknown/malformed; "
                    "original payload retained",
                    wire, n);
      }
    }
    if (changed) {
      n = dnp_lab_frame(wire, DNP3_FRAME_MAX, wire[3], src, dst, body, length);
      l->stats[dir].changed++;
      dnp_lab_log(l, dir, "EDIT", "edited; header/data CRCs rebuilt", wire, n);
    }
  }
  dnp_lab_fault_t *f = &l->config.faults[dir];
  uint32_t number = ++l->stats[dir].frames;
  if (nth(number, f->drop_every)) {
    l->stats[dir].dropped++;
    dnp_lab_log(l, dir, "DROP", "injected loss", wire, n);
    return;
  }
  if (nth(number, f->crc_every)) {
    wire[8] ^= 1;
    l->stats[dir].corrupted++;
    dnp_lab_log(l, dir, "FAULT", "injected header CRC error", wire, n);
  }
  if (nth(number, f->flip_every) && f->flip_offset < n) {
    wire[f->flip_offset] ^= f->flip_mask;
    l->stats[dir].corrupted++;
  }
  if (nth(number, f->truncate_every)) {
    size_t trim = f->truncate_bytes;
    if (trim >= n)
      trim = n;
    n -= trim;
    l->stats[dir].corrupted++;
  }
  enqueue(l, dir, wire, n, l->now + f->delay_ms);
  if (nth(number, f->duplicate_every)) {
    enqueue(l, dir, wire, n, l->now + f->delay_ms);
    l->stats[dir].duplicated++;
    dnp_lab_log(l, dir, "DUP", "injected duplicate", wire, n);
  }
}
/* Simulator and command parser are below; no socket, UART or RTOS dependency.
 */
static void simulate(dnp_lab_t *, unsigned, const uint8_t *, size_t);
static void consume(dnp_lab_t *l, unsigned dir) {
  dnp_lab_direction_t *d = &l->direction[dir];
  if (!d->used)
    return;
  if (d->count > DNP_LAB_QUEUE - 3 ||
      (l->config.mode != DNP_LAB_BRIDGE &&
       l->direction[1 - dir].count > DNP_LAB_QUEUE - 10)) {
    l->stats[dir].backpressure++;
    return;
  }
  if (l->config.mode == DNP_LAB_BRIDGE && !active(l, dir)) {
    size_t n = d->used;
    if (n > 256)
      n = 256;
    enqueue(l, dir, d->rx, n, l->now);
    memmove(d->rx, d->rx + n, d->used - n);
    d->used -= n;
    return;
  }
  size_t n = 0;
  if (d->used == 1 && d->rx[0] == 5 && l->now - d->touched < 1000)
    return;
  if (d->used >= 2 && d->rx[0] == 5 && d->rx[1] == 100) {
    if (d->used < 10) {
      if (l->now - d->touched < 1000)
        return;
      n = d->used;
      goto ready;
    }
    if (d->rx[2] >= 5 && dnp3_crc(d->rx, 8) == le(d->rx + 8, 2))
      n = 10 + (d->rx[2] - 5) + 2 * ((d->rx[2] - 5 + 15) / 16);
    else
      n = 1;
    if (n > d->used) {
      if (l->now - d->touched < 1000)
        return;
      n = d->used;
    }
  } else
    n = 1;
ready:
  if (n > DNP3_FRAME_MAX)
    n = DNP3_FRAME_MAX;
  if (l->config.mode == DNP_LAB_BRIDGE) {
    if (n >= 10)
      transform(l, dir, d->rx, n);
    else
      enqueue(l, dir, d->rx, n, l->now);
  } else
    simulate(l, dir, d->rx, n);
  memmove(d->rx, d->rx + n, d->used - n);
  d->used -= n;
}
void dnp_lab_step(dnp_lab_t *l, uint32_t now) {
  l->now = now;
  for (unsigned dir = 0; dir < 2; dir++) {
    for (unsigned i = 0; i < 16 && l->direction[dir].used; i++)
      consume(l, dir);
    dnp3_expire(&l->direction[dir].decode, now, 2000);
  }
  if ((l->config.mode == DNP_LAB_MASTER || l->config.mode == DNP_LAB_DEMO) &&
      l->config.interval_ms && !l->waiting &&
      now - l->last_poll >= l->config.interval_ms)
    dnp_lab_poll(l);
  if (l->waiting && now - l->request_at >= l->config.timeout_ms) {
    l->waiting = false;
    l->pending_operate = false;
    l->timeouts++;
    dnp_lab_log(l, 0, "TIMEOUT", "master response timeout", NULL, 0);
  }
}
static bool send_app(dnp_lab_t *l, unsigned dir, const uint8_t *app, size_t n) {
  if (!n || n > DNP3_FRAGMENT_MAX ||
      l->direction[dir].count + (n + 248) / 249 > DNP_LAB_QUEUE)
    return false;
  for (size_t at = 0; at < n;) {
    uint8_t payload[250], wire[DNP3_FRAME_MAX];
    size_t part = n - at;
    if (part > 249)
      part = 249;
    payload[0] = (at == 0 ? 128 : 0) | (at + part == n ? 64 : 0) |
                 (l->transport_sequence++ & 63);
    memcpy(payload + 1, app + at, part);
    unsigned length = dnp_lab_frame(
        wire, sizeof(wire), dir ? 0x44 : 0xc4,
        dir ? l->config.outstation : l->config.master,
        dir ? l->config.master : l->config.outstation, payload, part + 1);
    if (!enqueue(l, dir, wire, length, l->now))
      return false;
    at += part;
  }
  return true;
}
bool dnp_lab_poll(dnp_lab_t *l) {
  if (l->waiting ||
      (l->config.mode != DNP_LAB_MASTER && l->config.mode != DNP_LAB_DEMO))
    return false;
  uint8_t app[] = {0, 1, 60, 1, 6};
  app[0] = 0xc0 | (l->application_sequence++ & 15);
  if (!send_app(l, 0, app, sizeof(app)))
    return false;
  l->expected_sequence = app[0] & 15;
  l->expected_function = 1;
  l->waiting = true;
  l->pending_operate = false;
  l->request_at = l->last_poll = l->now;
  return true;
}
static bool control_request(dnp_lab_t *l, unsigned function) {
  uint8_t app[32];
  unsigned at = 0;
  app[at++] = 0xc0 | (l->application_sequence++ & 15);
  app[at++] = function;
  app[at++] = l->control_analog ? 41 : 12;
  app[at++] = 1;
  app[at++] = 0x28;
  put(app + at, 1, 2);
  at += 2;
  put(app + at, l->control_index, 2);
  at += 2;
  if (l->control_analog) {
    put(app + at, (uint32_t)l->control_value, 4);
    at += 4;
    app[at++] = 0;
  } else {
    app[at++] = l->control_value ? 3 : 4;
    app[at++] = 1;
    put(app + at, 100, 4);
    at += 4;
    put(app + at, 100, 4);
    at += 4;
    app[at++] = 0;
  }
  if (!send_app(l, 0, app, at))
    return false;
  l->waiting = true;
  l->expected_sequence = app[0] & 15;
  l->expected_function = function;
  l->request_at = l->now;
  memcpy(l->last_request, app, at);
  l->last_request_length = at;
  return true;
}
bool dnp_lab_control(dnp_lab_t *l, bool analog, uint16_t index, int32_t value,
                     bool direct) {
  if (l->waiting ||
      (l->config.mode != DNP_LAB_MASTER && l->config.mode != DNP_LAB_DEMO))
    return false;
  l->control_analog = analog;
  l->control_index = index;
  l->control_value = value;
  l->control_direct = direct;
  l->pending_operate = !direct;
  return control_request(l, direct ? 5 : 3);
}
static size_t static_objects(dnp_lab_t *l, uint8_t *p, unsigned g, unsigned v,
                             unsigned first, unsigned last) {
  unsigned count = l->config.points;
  if (!count || first >= count)
    return 0;
  if (last >= count)
    last = count - 1;
  if (last < first)
    return 0;
  p[0] = g;
  p[1] = v;
  p[2] = 0;
  p[3] = first;
  p[4] = last;
  size_t at = 5;
  for (unsigned i = first; i <= last; i++) {
    dnp_lab_point_t *x = &l->points[i];
    if (g == 1 || g == 10)
      p[at++] = (g == 1 ? x->flags : x->output_flags) |
                (g == 1 ? x->binary : x->binary_output) * 128;
    else {
      p[at++] = g == 30 ? x->analog_flags : x->analog_output_flags;
      put(p + at, (uint32_t)(g == 30 ? x->analog : x->analog_output), 4);
      at += 4;
    }
  }
  return at;
}
static void outstation(dnp_lab_t *l, const uint8_t *app, size_t n) {
  if (n < 2)
    return;
  if (app[1] && n == l->remote_request_length &&
      !memcmp(app, l->remote_request, n)) {
    if (l->remote_reply_length)
      send_app(l, 1, l->remote_reply, l->remote_reply_length);
    return;
  }
  uint8_t reply[DNP3_FRAGMENT_MAX];
  reply[0] = 0xc0 | (app[0] & 15);
  reply[1] = 129;
  reply[2] = reply[3] = 0;
  size_t at = 4;
  unsigned fn = app[1];
  if (fn == 0)
    return;
  if (fn == 1) {
    const uint8_t *p = app + 2;
    size_t left = n - 2;
    while (left) {
      if (left < 3) {
        reply[3] |= 4;
        break;
      }
      unsigned g = p[0], v = p[1], q = p[2], first = 0,
               last = l->config.points - 1;
      p += 3;
      left -= 3;
      if (q == 0) {
        if (left < 2) {
          reply[3] |= 4;
          break;
        }
        first = p[0];
        last = p[1];
        p += 2;
        left -= 2;
      } else if (q == 1) {
        if (left < 4) {
          reply[3] |= 4;
          break;
        }
        first = le(p, 2);
        last = le(p + 2, 2);
        p += 4;
        left -= 4;
      } else if (q != 6) {
        reply[3] |= 4;
        break;
      }
      if (first > last || first >= l->config.points) {
        reply[3] |= 4;
        continue;
      }
      if (g == 60 && v == 1) {
        for (unsigned k = 0; k < 4; k++) {
          unsigned group = k == 0 ? 1 : k == 1 ? 30 : k == 2 ? 10 : 40;
          at +=
              static_objects(l, reply + at, group,
                             (group == 1 || group == 10) ? 2 : 1, first, last);
        }
      } else if (g == 60 && v >= 2 &&
                 v <= 4) { /* no event queue: empty class scans */
      } else if ((g == 1 && (v == 0 || v == 2)) ||
                 (g == 10 && (v == 0 || v == 2)) ||
                 (g == 30 && (v == 0 || v == 1)) ||
                 (g == 40 && (v == 0 || v == 1)))
        at += static_objects(l, reply + at, g, (g == 1 || g == 10) ? 2 : 1,
                             first, last);
      else
        reply[3] |= 2;
      if (at > sizeof(reply) - 512) {
        reply[3] |= 4;
        break;
      }
    }
  } else if (fn == 3 || fn == 4 || fn == 5 || fn == 6) {
    /* One indexed CROB or int32 analog command per request. Echo the exact
     * object with a result status, and operate only after a matching SELECT. */
    unsigned status = 4;
    size_t base = 2;
    if (n >= 7 && app[3] == 1 && (app[2] == 12 || app[2] == 41) &&
        (app[4] == 0x17 || app[4] == 0x28)) {
      unsigned iw = app[4] == 0x17 ? 1 : 2, cw = iw;
      size_t count_at = 5, index_at = count_at + cw, value_at = index_at + iw;
      unsigned size = app[2] == 12 ? 11 : 5;
      if (n == value_at + size && le(app + count_at, cw) == 1) {
        unsigned index = le(app + index_at, iw), kind = app[2];
        int32_t value = kind == 12 ? (app[value_at] & 15) == 3
                                   : (int32_t)le(app + value_at, 4);
        status = index < l->config.points ? 0 : 4;
        if (kind == 12 && ((app[value_at] != 3 && app[value_at] != 4) ||
                           app[value_at + 1] != 1))
          status = 4;
        if (!status && fn == 3) {
          l->selected = true;
          l->selected_kind = kind;
          l->selected_index = index;
          l->selected_value = value;
          l->select_at = l->now;
        } else if (!status) {
          if (fn == 4 &&
              (!l->selected || l->selected_kind != kind ||
               l->selected_index != index || l->selected_value != value ||
               l->now - l->select_at > l->config.timeout_ms))
            status = 2;
          if (!status) {
            if (kind == 12)
              l->points[index].binary_output = !!value;
            else
              l->points[index].analog_output = value;
            l->controls++;
          }
          l->selected = false;
        }
      }
    }
    if (n - 2 > sizeof(reply) - 4) {
      reply[3] |= 4;
    } else {
      memcpy(reply + 4, app + base, n - base);
      at = n + 2;
      if (at > 4)
        reply[at - 1] = status;
    }
    if (status)
      l->errors++;
    if (fn == 6) {
      memcpy(l->remote_request, app, n);
      l->remote_request_length = n;
      l->remote_reply_length = 0;
      return;
    }
  } else if (fn == 20 || fn == 21 ||
             fn == 22) { /* freeze/event management not implemented */
    reply[3] |= 1;
  } else
    reply[3] |= 1;
  memcpy(l->remote_request, app, n);
  l->remote_request_length = n;
  memcpy(l->remote_reply, reply, at);
  l->remote_reply_length = at;
  if (!send_app(l, 1, reply, at)) {
    l->errors++;
    dnp_lab_log(l, 1, "ERROR", "outstation TX queue full", NULL, 0);
  }
}
static void master(dnp_lab_t *l, const uint8_t *app, size_t n) {
  if (n < 4 || (app[1] != 129 && app[1] != 130))
    return;
  l->responses++;
  if (app[0] & 32) {
    uint8_t confirm[] = {0, 0};
    confirm[0] = 0xc0 | (app[0] & 31);
    send_app(l, 0, confirm, 2);
  }
  bool ignored = false;
  if (objects(l, 1, (uint8_t *)app + 4, n - 4, 2, &ignored))
    objects(l, 1, (uint8_t *)app + 4, n - 4, 0, &ignored);
  if (!l->waiting || app[1] != 129 || (app[0] & 15) != l->expected_sequence)
    return;
  l->waiting = false;
  if (app[3]) {
    l->errors++;
    dnp_lab_log(l, 1, "IIN", "remote reports application error", NULL, 0);
  }
  if (l->pending_operate) {
    bool same = n == l->last_request_length + 2 && !(le(app + 2, 2) & 0xff00) &&
                n > 4 && !memcmp(app + 4, l->last_request + 2, n - 5) &&
                app[n - 1] == 0;
    l->pending_operate = false;
    if (same) {
      if (!control_request(l, 4))
        l->errors++;
    } else {
      l->errors++;
      dnp_lab_log(l, 1, "ERROR", "SELECT rejected/mismatched; OPERATE not sent",
                  NULL, 0);
    }
  } else if (l->expected_function == 4 || l->expected_function == 5) {
    bool same = n == l->last_request_length + 2 && !app[3] && n > 4 &&
                !memcmp(app + 4, l->last_request + 2, n - 5) && app[n - 1] == 0;
    if (same) {
      if (l->config.mode == DNP_LAB_MASTER)
        l->controls++;
      dnp_lab_log(l, 1, "CONTROL", "control completed", NULL, 0);
    } else {
      l->errors++;
      dnp_lab_log(l, 1, "ERROR", "control rejected/mismatched", NULL, 0);
    }
  }
}
static void simulate(dnp_lab_t *l, unsigned dir, const uint8_t *wire,
                     size_t size) {
  uint8_t body[250];
  int n = dnp_lab_unpack(wire, size, body);
  if (n < 0)
    return;
  unsigned src = le(wire + 6, 2), dst = le(wire + 4, 2), ctl = wire[3],
           fn = ctl & 15;
  if (src != (dir ? l->config.outstation : l->config.master) ||
      dst != (dir ? l->config.master : l->config.outstation))
    return;
  if ((dir == 0 && l->config.mode == DNP_LAB_MASTER) ||
      (dir == 1 && l->config.mode == DNP_LAB_OUTSTATION))
    return;
  if (!dir && (ctl & 64) && (fn == 0 || fn == 9 || fn == 3)) {
    uint8_t ack[10];
    size_t ack_n =
        dnp_lab_frame(ack, sizeof(ack), fn == 9 ? 0x0b : 0,
                      l->config.outstation, l->config.master, NULL, 0);
    enqueue(l, 1, ack, ack_n, l->now);
    if (fn == 0) {
      l->have_rx_fcb = false;
      return;
    }
    if (fn == 9)
      return;
    unsigned fcb = !!(ctl & 32);
    if ((ctl & 16) && l->have_rx_fcb && l->rx_fcb == fcb)
      return;
    l->have_rx_fcb = true;
    l->rx_fcb = fcb;
  }
  if (!(ctl & 64) || (fn != 3 && fn != 4) || !n)
    return;
  unsigned seq = body[0] & 63;
  if (body[0] & 128) {
    l->assembly[dir].used = 0;
    l->assembly[dir].active = true;
    l->assembly[dir].expected = seq;
  }
  if (!l->assembly[dir].active || seq != l->assembly[dir].expected ||
      (unsigned)(n - 1) >
          (unsigned)(DNP3_FRAGMENT_MAX - l->assembly[dir].used)) {
    l->assembly[dir].active = false;
    l->errors++;
    return;
  }
  memcpy(l->assembly[dir].data + l->assembly[dir].used, body + 1, n - 1);
  l->assembly[dir].used += n - 1;
  l->assembly[dir].expected = (seq + 1) & 63;
  if (body[0] & 64) {
    l->assembly[dir].active = false;
    const uint8_t *app = l->assembly[dir].data;
    size_t used = l->assembly[dir].used;
    if (used < 2 || (app[0] & 0xc0) != 0xc0) {
      l->errors++;
      return;
    }
    if (dir)
      master(l, app, used);
    else
      outstation(l, app, used);
  }
}
static bool number(const char *s, long low, long high, long *out) {
  if (!s || !*s)
    return false;
  char *end;
  errno = 0;
  long n = strtol(s, &end, 0);
  if (errno || *end || n < low || n > high)
    return false;
  *out = n;
  return true;
}
static bool decimal(const char *s, double *out) {
  if (!s || !*s)
    return false;
  char *end;
  errno = 0;
  double n = strtod(s, &end);
  if (errno || *end || !isfinite(n))
    return false;
  *out = n;
  return true;
}
static int direction(const char *s) {
  return !strcmp(s, "a2b") ? 0 : !strcmp(s, "b2a") ? 1 : -1;
}
static int format(const char *s) {
  const char *names[] = {"8n1", "7e1", "7o1", "8e1", "8o1",
                         "8n2", "7e2", "7o2", "8e2", "8o2"};
  const int codes[] = {0, 2, 3, 6, 7, 256, 258, 259, 262, 263};
  for (unsigned i = 0; i < 10; i++)
    if (!strcmp(s, names[i]))
      return codes[i];
  return -1;
}
bool dnp_lab_command(dnp_lab_t *l, const char *line, bool running, char *out,
                     size_t capacity) {
  char text[224];
  char *v[12], *save = NULL;
  unsigned n = 0;
  long a = 0, b = 0, c = 0;
  double value = 0;
  if (!line || strlen(line) >= sizeof(text))
    goto usage;
  strcpy(text, line);
  for (char *p = strtok_r(text, " \t\r\n", &save); p;
       p = strtok_r(NULL, " \t\r\n", &save)) {
    if (n == 12)
      goto usage;
    v[n++] = p;
  }
  if (!n)
    goto usage;
  if (!strcmp(v[0], "mode") && n == 2) {
    if (running)
      goto stopped;
    const char *names[] = {"bridge", "master", "outstation", "demo"};
    for (unsigned i = 0; i < 4; i++)
      if (!strcmp(v[1], names[i])) {
        l->config.mode = i;
        goto ok;
      }
    goto usage;
  }
  if ((!strcmp(v[0], "a") || !strcmp(v[0], "b")) && n >= 4) {
    if (running)
      goto stopped;
    unsigned which = v[0][0] == 'b';
    dnp_lab_endpoint_config_t next = l->config.endpoint[which];
    if (!strcmp(v[1], "serial") && (n == 4 || n == 5)) {
      const char *ports[] = {"uart7", "uart8", "uart3"};
      int slot = -1;
      for (unsigned i = 0; i < 3; i++)
        if (!strcmp(v[2], ports[i]))
          slot = i;
      int fmt = n == 5 ? format(v[4]) : 0;
      if (slot < 0 || fmt < 0 || !number(v[3], 300, 1000000, &a))
        goto usage;
      next.kind = DNP_LAB_SERIAL;
      next.slot = slot;
      next.baud = a;
      next.format = fmt;
    } else if (!strcmp(v[1], "tcp") && !strcmp(v[2], "listen") && n == 4) {
      if (!number(v[3], 1, 65535, &a))
        goto usage;
      next.kind = DNP_LAB_LISTEN;
      next.port = a;
    } else if (!strcmp(v[1], "tcp") && !strcmp(v[2], "connect") && n == 5) {
      if (!*v[3] || strlen(v[3]) >= sizeof(next.host) ||
          !number(v[4], 1, 65535, &a))
        goto usage;
      next.kind = DNP_LAB_CONNECT;
      next.port = a;
      strcpy(next.host, v[3]);
    } else
      goto usage;
    l->config.endpoint[which] = next;
    goto ok;
  }
  if (!strcmp(v[0], "addresses") && n == 3) {
    if (running)
      goto stopped;
    if (!number(v[1], 0, 65519, &a) || !number(v[2], 0, 65519, &b) || a == b)
      goto usage;
    l->config.master = a;
    l->config.outstation = b;
    goto ok;
  }
  if (!strcmp(v[0], "interval") && n == 2 && number(v[1], 0, 60000, &a)) {
    l->config.interval_ms = a;
    goto ok;
  }
  if (!strcmp(v[0], "timeout") && n == 2 && number(v[1], 100, 60000, &a)) {
    l->config.timeout_ms = a;
    goto ok;
  }
  if (!strcmp(v[0], "points") && n == 2 &&
      number(v[1], 1, DNP_LAB_POINTS, &a)) {
    if (running)
      goto stopped;
    l->config.points = a;
    goto ok;
  }
  if (!strcmp(v[0], "point") && (n == 4 || n == 5) &&
      number(v[2], 0, DNP_LAB_POINTS - 1, &a) &&
      number(n == 5 ? v[4] : "1", 0, 127, &b)) {
    bool binary = !strcmp(v[1], "bi") || !strcmp(v[1], "bo"),
         analog = !strcmp(v[1], "ai") || !strcmp(v[1], "ao");
    if (!binary && !analog)
      goto usage;
    if (!number(v[3], binary ? 0 : INT32_MIN, binary ? 1 : INT32_MAX, &c))
      goto usage;
    dnp_lab_point_t *p = &l->points[a];
    if (!strcmp(v[1], "bi")) {
      p->binary = c;
      p->flags = b;
    }
    if (!strcmp(v[1], "bo")) {
      p->binary_output = c;
      p->output_flags = b;
    }
    if (!strcmp(v[1], "ai")) {
      p->analog = c;
      p->analog_flags = b;
    }
    if (!strcmp(v[1], "ao")) {
      p->analog_output = c;
      p->analog_output_flags = b;
    }
    goto ok;
  }
  if (!strcmp(v[0], "poll") && n == 1) {
    if (!running || !dnp_lab_poll(l))
      goto busy;
    goto ok;
  }
  if (!strcmp(v[0], "control") && (n == 4 || n == 5) &&
      number(v[2], 0, 65535, &a)) {
    bool analog = !strcmp(v[1], "analog");
    if (!analog && strcmp(v[1], "binary"))
      goto usage;
    if (!number(v[3], analog ? INT32_MIN : 0, analog ? INT32_MAX : 1, &b))
      goto usage;
    bool direct = n == 5 && !strcmp(v[4], "direct");
    if (n == 5 && !direct && strcmp(v[4], "sbo"))
      goto usage;
    if (!running || !dnp_lab_control(l, analog, a, b, direct))
      goto busy;
    goto ok;
  }
  if (!strcmp(v[0], "rule")) {
    if (n == 2 && !strcmp(v[1], "clear")) {
      memset(l->config.rules, 0, sizeof(l->config.rules));
      goto ok;
    }
    if (n == 3 && !strcmp(v[1], "del") &&
        number(v[2], 0, DNP_LAB_RULES - 1, &a)) {
      memset(&l->config.rules[a], 0, sizeof(l->config.rules[a]));
      goto ok;
    }
    dnp_lab_rule_t r = {0};
    r.enabled = true;
    r.flags = -1;
    if (n >= 6 && !strcmp(v[1], "add")) {
      int dir = direction(v[3]);
      if (dir < 0)
        goto usage;
      r.direction = dir;
      if (!strcmp(v[2], "link") && n == 7 && number(v[5], 0, 65535, &a) &&
          number(v[6], 0, 65535, &b)) {
        if (!strcmp(v[4], "src"))
          r.kind = DNP_LAB_LINK_SRC;
        else if (!strcmp(v[4], "dst"))
          r.kind = DNP_LAB_LINK_DST;
        else
          goto usage;
        r.index = a;
        r.replacement = b;
      } else if (!strcmp(v[2], "control") && n == 6 &&
                 number(v[4], 0, 65535, &a) && number(v[5], 0, 65535, &b)) {
        r.kind = DNP_LAB_CONTROL_INDEX;
        r.index = a;
        r.replacement = b;
      } else if ((!strcmp(v[2], "binary") || !strcmp(v[2], "analog")) &&
                 (n == 6 || n == 7) && number(v[4], 0, 65535, &a) &&
                 decimal(v[5], &value)) {
        r.kind = !strcmp(v[2], "binary") ? DNP_LAB_BINARY : DNP_LAB_ANALOG;
        r.index = a;
        r.value = value;
        if (r.kind == DNP_LAB_BINARY && value != 0 && value != 1)
          goto usage;
        if (n == 7) {
          if (!number(v[6], 0, 255, &b))
            goto usage;
          r.flags = b;
        }
      } else
        goto usage;
      for (unsigned i = 0; i < DNP_LAB_RULES; i++)
        if (!l->config.rules[i].enabled) {
          l->config.rules[i] = r;
          snprintf(out, capacity, "Rule %u enabled (%s)", i,
                   dir ? "B->A" : "A->B");
          return true;
        }
      snprintf(out, capacity, "Rule table full");
      return false;
    }
  }
  if (!strcmp(v[0], "fault")) {
    if (n == 2 && !strcmp(v[1], "clear")) {
      memset(l->config.faults, 0, sizeof(l->config.faults));
      goto ok;
    }
    if (n < 4)
      goto usage;
    int dir = direction(v[1]);
    if (dir < 0)
      goto usage;
    dnp_lab_fault_t f = l->config.faults[dir];
    if (n == 4 && number(v[3], 0, 60000, &a)) {
      if (!strcmp(v[2], "drop"))
        f.drop_every = a;
      else if (!strcmp(v[2], "duplicate"))
        f.duplicate_every = a;
      else if (!strcmp(v[2], "crc"))
        f.crc_every = a;
      else if (!strcmp(v[2], "delay"))
        f.delay_ms = a;
      else
        goto usage;
    } else if (n == 5 && !strcmp(v[2], "truncate") &&
               number(v[3], 0, DNP3_FRAME_MAX, &a) &&
               number(v[4], 0, 60000, &b)) {
      f.truncate_bytes = a;
      f.truncate_every = b;
    } else if (n == 6 && !strcmp(v[2], "flip") &&
               number(v[3], 0, DNP3_FRAME_MAX - 1, &a) &&
               number(v[4], 1, 255, &b) && number(v[5], 0, 60000, &c)) {
      f.flip_offset = a;
      f.flip_mask = b;
      f.flip_every = c;
    } else
      goto usage;
    l->config.faults[dir] = f;
    goto ok;
  }
usage:
  snprintf(out, capacity, "Invalid command; press ? for syntax");
  return false;
stopped:
  snprintf(
      out, capacity,
      "Stop transport before changing endpoints/role/addresses/point count");
  return false;
busy:
  snprintf(out, capacity, "Master unavailable, stopped or awaiting a response");
  return false;
ok:
  snprintf(out, capacity, "OK");
  return true;
}
