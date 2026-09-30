#pragma once
#include "solar_os_isotp.h"
#include <stdio.h>
#define SOLAR_OBD_ECUS 8
#define SOLAR_OBD_CODES 32
typedef struct {
    bool seen;
    uint8_t valid, negative[4], readiness[4], count[3];
    uint16_t codes[3][SOLAR_OBD_CODES];
    uint8_t clear_result; // 0 not requested, 1 acknowledged, 2 timeout, 3 rejected
    uint8_t clear_nrc;
} solar_obd_ecu_t;
typedef struct {
    solar_can_t *bus;
    solar_isotp_t links[SOLAR_OBD_ECUS];
    solar_obd_ecu_t ecus[SOLAR_OBD_ECUS];
    uint32_t deadline, errors, timeouts, rx_frames, tx_frames;
    uint8_t phase, selected, channel;
    bool active, clearing, done;
} solar_obd_t;
void solar_obd_init(solar_obd_t *o, solar_can_t *bus, uint8_t channel);
bool solar_obd_scan(solar_obd_t *o, uint32_t now);
bool solar_obd_clear(solar_obd_t *o, unsigned ecu, uint32_t now);
void solar_obd_poll(solar_obd_t *o, uint32_t now);
void solar_obd_dtc(uint16_t code, char out[6]);
/* Decoder consumes CAN OBD payloads, including the DTC count byte. */
bool solar_obd_decode(solar_obd_ecu_t *e, unsigned phase, const uint8_t *p, size_t n);
bool solar_obd_report(const solar_obd_t *o, FILE *out, const char *scenario);
