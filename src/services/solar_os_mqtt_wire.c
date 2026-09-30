#include "solar_os_mqtt_wire.h"
#include <string.h>
void solar_os_mqtt_wire_init(solar_os_mqtt_wire_t *p) {
    memset(p, 0, sizeof(*p));
    p->multiplier = 1;
}
static bool valid_header(uint8_t h) {
    switch (h >> 4) {
    case 3:
        return ((h >> 1) & 3) <= 1 && !((h & 6) == 0 && (h & 8));
    case 2:
    case 9:
    case 13:
        return !(h & 15);
    default:
        return false;
    }
}
bool solar_os_mqtt_wire_feed(solar_os_mqtt_wire_t *p, const uint8_t *data, size_t n,
                             solar_os_mqtt_wire_event_fn event, void *user) {
    for (size_t at = 0; at < n; ++at) {
        uint8_t b = data[at];
        if (p->phase == 0) {
            if (!valid_header(b))
                return false;
            p->header = b;
            p->phase = 1;
            continue;
        }
        if (p->phase == 1) {
            p->remaining += (b & 127) * p->multiplier;
            if (++p->length_bytes > 4 || (p->length_bytes == 4 && (b & 128)))
                return false;
            if (b & 128) {
                p->multiplier *= 128;
                continue;
            }
            // Bound hostile packet drain time, independently of retained payload capacity.
            if (p->remaining > 1024U * 1024U || (p->length_bytes > 1 && !(b & 127)))
                return false;
            unsigned kind = p->header >> 4;
            if ((kind == 2 && p->remaining != 2) || (kind == 9 && p->remaining != 4) ||
                (kind == 13 && p->remaining) || (kind == 3 && p->remaining < 3))
                return false;
            p->phase = 2;
            if (p->remaining)
                continue;
        } else {
            unsigned pos = p->position++;
            if ((p->header >> 4) == 3) {
                if (pos == 0)
                    p->topic_length = (uint16_t)b << 8;
                else if (pos == 1) {
                    p->topic_length |= b;
                    unsigned overhead = 2U + p->topic_length + ((p->header & 6) ? 2U : 0U);
                    if (!p->topic_length || overhead > p->remaining)
                        return false;
                    p->message.topic_size = p->topic_length;
                    p->message.payload_size = p->remaining - overhead;
                    p->message.qos = (p->header >> 1) & 3;
                    p->message.retained = p->header & 1;
                    p->message.duplicate = p->header & 8;
                    p->message.truncated = p->topic_length >= MQTT_CAPTURE_TOPIC_BYTES ||
                                           p->message.payload_size > MQTT_CAPTURE_PAYLOAD_BYTES;
                } else if (pos < 2U + p->topic_length) {
                    if (!b || b == '#' || b == '+')
                        return false;
                    if (pos - 2 < MQTT_CAPTURE_TOPIC_BYTES - 1)
                        p->message.topic[pos - 2] = (char)b;
                } else if (p->message.qos && pos < 4U + p->topic_length) {
                    p->packet_id = (p->packet_id << 8) | b;
                } else if (p->message.stored_size < MQTT_CAPTURE_PAYLOAD_BYTES) {
                    p->message.payload[p->message.stored_size++] = b;
                }
            } else if (pos < sizeof(p->control))
                p->control[pos] = b;
            if (p->position < p->remaining)
                continue;
        }
        if ((p->header >> 4) == 3 && p->message.qos && !p->packet_id)
            return false;
        if (!event(user, p->header, p->packet_id, p->control, p->remaining,
                   (p->header >> 4) == 3 ? &p->message : NULL))
            return false;
        solar_os_mqtt_wire_init(p);
    }
    return true;
}
static bool string(uint8_t *out, size_t size, size_t *at, const char *s) {
    size_t n = strlen(s);
    if (n > 65535 || *at + 2 + n > size)
        return false;
    out[(*at)++] = n >> 8;
    out[(*at)++] = n;
    memcpy(out + *at, s, n);
    *at += n;
    return true;
}
static size_t packet(uint8_t *out, size_t size, uint8_t header, const uint8_t *body, size_t len) {
    size_t at = 0, n = len;
    if (size < 5 + len)
        return 0;
    out[at++] = header;
    do {
        uint8_t b = n % 128;
        n /= 128;
        out[at++] = b | (n ? 128 : 0);
    } while (n);
    memcpy(out + at, body, len);
    return at + len;
}
size_t solar_os_mqtt_wire_connect(uint8_t *out, size_t size, const char *client, const char *user,
                                  const char *password) {
    uint8_t body[384] = {0, 4, 'M', 'Q', 'T', 'T', 4, 2, 0, 30};
    size_t n = 10;
    if (*user)
        body[7] |= 128;
    if (*password) {
        if (!*user)
            return 0;
        body[7] |= 64;
    }
    if (!string(body, sizeof(body), &n, client) ||
        (*user && !string(body, sizeof(body), &n, user)) ||
        (*password && !string(body, sizeof(body), &n, password)))
        return 0;
    return packet(out, size, 0x10, body, n);
}
size_t solar_os_mqtt_wire_subscribe(uint8_t *out, size_t size, uint16_t id) {
    uint8_t body[] = {id >> 8, id, 0, 1, '#', 1, 0, 6, '$', 'S', 'Y', 'S', '/', '#', 1};
    return id ? packet(out, size, 0x82, body, sizeof(body)) : 0;
}
