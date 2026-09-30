#include "solar_os_mqtt_wire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned events;
static solar_os_mqtt_capture_model_t *model;
static bool event(void *unused, uint8_t header, uint16_t id, const uint8_t *b, size_t n,
                  const solar_os_mqtt_capture_message_t *m) {
    (void)unused;
    (void)b;
    (void)n;
    ++events;
    if ((header >> 4) == 3) {
        if (m->qos)
            assert(id == 0x1234);
        solar_os_mqtt_capture_record(model, m);
    }
    return true;
}
static size_t publish(uint8_t *out, const char *topic, const uint8_t *data, size_t size,
                      uint8_t flags) {
    size_t t = strlen(topic), remaining = 2 + t + size + ((flags & 6) ? 2 : 0), at = 0;
    out[at++] = 0x30 | flags;
    do {
        uint8_t b = remaining % 128;
        remaining /= 128;
        out[at++] = b | (remaining ? 128 : 0);
    } while (remaining);
    out[at++] = t >> 8;
    out[at++] = t;
    memcpy(out + at, topic, t);
    at += t;
    if (flags & 6) {
        out[at++] = 0x12;
        out[at++] = 0x34;
    }
    memcpy(out + at, data, size);
    return at + size;
}
int main(void) {
    model = calloc(1, sizeof(*model));
    assert(model);
    solar_os_mqtt_wire_t *p = calloc(1, sizeof(*p));
    assert(p);
    uint8_t wire[8000], payload[4096];
    for (unsigned i = 0; i < sizeof(payload); ++i)
        payload[i] = i;
    size_t n = publish(wire, "room/sensor", payload, sizeof(payload), 3);
    for (size_t split = 0; split <= n; ++split) {
        solar_os_mqtt_wire_init(p);
        events = 0;
        assert(solar_os_mqtt_wire_feed(p, wire, split, event, NULL));
        assert(solar_os_mqtt_wire_feed(p, wire + split, n - split, event, NULL));
        assert(events == 1);
        const solar_os_mqtt_capture_message_t *m =
            solar_os_mqtt_capture_find(model, model->received);
        assert(m && m->payload_size == 4096 && m->stored_size == 2048 && m->retained &&
               m->qos == 1 && m->truncated);
        assert(!memcmp(m->payload, payload, 2048) && !strcmp(m->topic, "room/sensor"));
    }
    solar_os_mqtt_wire_init(p);
    events = 0;
    for (size_t i = 0; i < n; ++i)
        assert(solar_os_mqtt_wire_feed(p, wire + i, 1, event, NULL));
    assert(events == 1);
    n = publish(wire, "empty", payload, 0, 0);
    n += publish(wire + n, "a//b/", payload, 3, 0);
    solar_os_mqtt_wire_init(p);
    events = 0;
    assert(solar_os_mqtt_wire_feed(p, wire, n, event, NULL));
    assert(events == 2);
    assert(!solar_os_mqtt_capture_find(model, 1));
    assert(model->evicted > 0);
    bool found = false;
    for (unsigned i = 0; i < model->topic_count; ++i)
        if (!strcmp(model->topics[i].path, "room/sensor")) {
            assert(model->topics[i].latest.payload_size == 4096);
            found = true;
        }
    assert(found);
    // Wire CONNECT and SUBSCRIBE verified against independent protocol bytes.
    n = solar_os_mqtt_wire_connect(wire, sizeof(wire), "test", "u", "p");
    static const uint8_t connect[] = {0x10, 22, 0,   4,   'M', 'Q', 'T', 'T', 4,   0xc2, 0, 30,
                                      0,    4,  't', 'e', 's', 't', 0,   1,   'u', 0,    1, 'p'};
    assert(n == sizeof(connect) && !memcmp(wire, connect, n));
    n = solar_os_mqtt_wire_subscribe(wire, sizeof(wire), 1);
    static const uint8_t sub[] = {0x82, 15,  0,   1,   0,   1,   '#', 1, 0,
                                  6,    '$', 'S', 'Y', 'S', '/', '#', 1};
    assert(n == sizeof(sub) && !memcmp(wire, sub, n));
    static const uint8_t malformed[][8] = {{0x30, 0},
                                           {0x36, 0},
                                           {0x30, 0x80, 0x80, 0x80, 0x80},
                                           {0x30, 3, 0, 4, 'a'},
                                           {0x20, 3, 0, 0, 0},
                                           {0x30, 3, 0, 1, 0},
                                           {0x30, 3, 0, 1, '#'}};
    const unsigned lengths[] = {2, 2, 5, 5, 5, 5, 5};
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        solar_os_mqtt_wire_init(p);
        assert(!solar_os_mqtt_wire_feed(p, malformed[i], lengths[i], event, NULL));
    }
    char long_topic[400];
    memset(long_topic, 'a', sizeof(long_topic) - 1);
    long_topic[399] = 0;
    n = publish(wire, long_topic, payload, 1, 0);
    solar_os_mqtt_wire_init(p);
    assert(solar_os_mqtt_wire_feed(p, wire, n, event, NULL));
    assert(model->unindexed);
    for (unsigned i = 0; i < 300; ++i) {
        char topic[32];
        snprintf(topic, sizeof(topic), "test/%u", i);
        n = publish(wire, topic, payload, 1, 0);
        solar_os_mqtt_wire_init(p);
        assert(solar_os_mqtt_wire_feed(p, wire, n, event, NULL));
    }
    assert(model->topic_count == MQTT_CAPTURE_TOPICS && model->unindexed > 1);
    // Deterministic malformed input fuzzing exercises bounds under sanitizers.
    uint32_t rng = 123;
    for (unsigned i = 0; i < 20000; ++i) {
        for (unsigned j = 0; j < 64; ++j) {
            rng = rng * 1664525 + 1013904223;
            wire[j] = rng >> 24;
        }
        solar_os_mqtt_wire_init(p);
        solar_os_mqtt_wire_feed(p, wire, 64, event, NULL);
    }
    free(p);
    free(model);
    puts("PASS: MQTT fragmentation, binary/oversized payloads, protocol vectors, malformed input, "
         "bounded tree/history and fuzz");
}
