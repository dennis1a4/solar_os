#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MQTT_CAPTURE_TOPICS 256
#define MQTT_CAPTURE_HISTORY 256
#define MQTT_CAPTURE_TOPIC_BYTES 256
#define MQTT_CAPTURE_PAYLOAD_BYTES 2048

typedef struct {
    uint64_t sequence;
    uint32_t received_ms, payload_size;
    uint16_t topic_size, stored_size;
    uint8_t qos;
    bool retained, duplicate, truncated;
    char topic[MQTT_CAPTURE_TOPIC_BYTES];
    uint8_t payload[MQTT_CAPTURE_PAYLOAD_BYTES];
} solar_os_mqtt_capture_message_t;

typedef struct {
    char path[MQTT_CAPTURE_TOPIC_BYTES];
    int16_t parent;
    uint64_t count;
    solar_os_mqtt_capture_message_t latest;
} solar_os_mqtt_capture_topic_t;

typedef struct {
    uint64_t received, evicted, truncated, unindexed;
    uint16_t topic_count;
    solar_os_mqtt_capture_topic_t topics[MQTT_CAPTURE_TOPICS];
    solar_os_mqtt_capture_message_t history[MQTT_CAPTURE_HISTORY];
} solar_os_mqtt_capture_model_t;

// Pure bounded model; caller serializes access. Latest values survive history eviction.
void solar_os_mqtt_capture_record(solar_os_mqtt_capture_model_t *,
                                  const solar_os_mqtt_capture_message_t *);
const solar_os_mqtt_capture_message_t *
solar_os_mqtt_capture_find(const solar_os_mqtt_capture_model_t *, uint64_t sequence);

typedef struct {
    char host[128], username[64], password[96];
    uint16_t port;
} solar_os_mqtt_capture_config_t;
typedef struct solar_os_mqtt_capture solar_os_mqtt_capture_t;
typedef struct {
    bool connected;
    uint32_t connections, interruptions, protocol_errors;
    char detail[96];
} solar_os_mqtt_capture_status_t;

esp_err_t solar_os_mqtt_capture_start(const solar_os_mqtt_capture_config_t *,
                                      solar_os_mqtt_capture_t **);
// Blocks until cooperative worker exit; never frees a live worker's storage.
void solar_os_mqtt_capture_halt(solar_os_mqtt_capture_t *);
void solar_os_mqtt_capture_stop(solar_os_mqtt_capture_t *);
solar_os_mqtt_capture_model_t *solar_os_mqtt_capture_lock(solar_os_mqtt_capture_t *,
                                                          solar_os_mqtt_capture_status_t *);
void solar_os_mqtt_capture_unlock(solar_os_mqtt_capture_t *);
