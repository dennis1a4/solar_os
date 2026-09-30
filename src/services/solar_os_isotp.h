#pragma once
#include "solar_os_can.h"
#define SOLAR_ISOTP_MAX 256
/* Normal addressing, Classical CAN. IDs may be 11 or 29 bit. Millisecond clock;
 * sub-ms STmin requests are conservatively rounded up to one millisecond. */
typedef bool (*solar_isotp_emit_t)(void *, const solar_can_frame_t *);
typedef struct {
    uint32_t rx_id, tx_id, rx_deadline, tx_deadline, next_tx;
    uint8_t channel;
    bool extended, receiving, complete, transmitting, waiting_fc;
    uint8_t rx_sn, tx_sn, block_size, block_left, waits, separation_ms;
    uint16_t rx_size, rx_used, tx_size, tx_used;
    uint8_t rx[SOLAR_ISOTP_MAX], tx[SOLAR_ISOTP_MAX];
    uint32_t errors, timeouts;
    solar_isotp_emit_t emit;
    void *user;
} solar_isotp_t;
void solar_isotp_init(solar_isotp_t *s, uint32_t rx_id, uint32_t tx_id, uint8_t channel,
                      bool extended, solar_isotp_emit_t emit, void *user);
bool solar_isotp_send(solar_isotp_t *s, const uint8_t *data, size_t size, uint32_t now);
void solar_isotp_receive(solar_isotp_t *s, const solar_can_frame_t *f, uint32_t now);
void solar_isotp_poll(solar_isotp_t *s, uint32_t now);
