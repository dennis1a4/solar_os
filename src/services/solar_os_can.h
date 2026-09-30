#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SOLAR_CAN_QUEUE 32
/* Classical CAN only. Controller timestamps are supplied by the backend. */
typedef struct {
    uint32_t id, timestamp_ms;
    uint8_t channel, length;
    bool extended, remote;
    uint8_t data[8];
} solar_can_frame_t;
typedef struct {
    solar_can_frame_t frames[SOLAR_CAN_QUEUE];
    unsigned head, count;
    uint32_t dropped;
} solar_can_queue_t;
typedef struct {
    solar_can_queue_t rx, tx;
    bool enabled[2], listen_only[2];
    uint32_t rejected;
} solar_can_t;
/* Single owner. Hardware backend must serialize ingress with its worker.
 * No implicit hardware discovery, bitrate probing or global mutable state. */
bool solar_can_valid(const solar_can_frame_t *f);
bool solar_can_push(solar_can_queue_t *q, const solar_can_frame_t *f);
bool solar_can_pop(solar_can_queue_t *q, solar_can_frame_t *f);
void solar_can_init(solar_can_t *bus);
bool solar_can_send(solar_can_t *bus, const solar_can_frame_t *f);
