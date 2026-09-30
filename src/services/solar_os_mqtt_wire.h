#pragma once
#include "solar_os_mqtt_capture.h"
// Incremental MQTT 3.1.1 decoder. Oversized payloads are drained while retaining
// a bounded prefix; packet framing never depends on TCP segmentation.
typedef struct {
    uint8_t header, phase, length_bytes;
    uint32_t remaining, multiplier, position;
    uint16_t topic_length, packet_id;
    uint8_t control[8];
    solar_os_mqtt_capture_message_t message;
} solar_os_mqtt_wire_t;
typedef bool (*solar_os_mqtt_wire_event_fn)(void *, uint8_t header, uint16_t packet_id,
                                            const uint8_t *control, size_t control_size,
                                            const solar_os_mqtt_capture_message_t *message);
void solar_os_mqtt_wire_init(solar_os_mqtt_wire_t *);
bool solar_os_mqtt_wire_feed(solar_os_mqtt_wire_t *, const uint8_t *, size_t,
                             solar_os_mqtt_wire_event_fn, void *);
size_t solar_os_mqtt_wire_connect(uint8_t *, size_t, const char *client, const char *user,
                                  const char *password);
size_t solar_os_mqtt_wire_subscribe(uint8_t *, size_t, uint16_t id);
