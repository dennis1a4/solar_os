#include "solar_os_isotp.h"
#include <string.h>
#define TIMEOUT 1000U
static bool due(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }
static bool emit(solar_isotp_t *s, const uint8_t *p, size_t n, uint32_t now) {
    solar_can_frame_t f = {.id = s->tx_id,
                           .timestamp_ms = now,
                           .channel = s->channel,
                           .extended = s->extended,
                           .length = 8};
    memcpy(f.data, p, n);
    if (s->emit(s->user, &f))
        return true;
    ++s->errors;
    s->transmitting = s->receiving = false;
    return false;
}
static void bad_rx(solar_isotp_t *s) {
    ++s->errors;
    s->receiving = false;
    s->complete = false;
}
void solar_isotp_init(solar_isotp_t *s, uint32_t rx_id, uint32_t tx_id, uint8_t channel,
                      bool extended, solar_isotp_emit_t callback, void *user) {
    memset(s, 0, sizeof(*s));
    s->rx_id = rx_id;
    s->tx_id = tx_id;
    s->channel = channel;
    s->extended = extended;
    s->emit = callback;
    s->user = user;
}
bool solar_isotp_send(solar_isotp_t *s, const uint8_t *data, size_t size, uint32_t now) {
    if (!data || !size || size > SOLAR_ISOTP_MAX || s->transmitting)
        return false;
    uint8_t p[8] = {0};
    if (size <= 7) {
        p[0] = (uint8_t)size;
        memcpy(p + 1, data, size);
        return emit(s, p, size + 1, now);
    }
    memcpy(s->tx, data, size);
    s->tx_size = size;
    s->tx_used = 6;
    s->tx_sn = 1;
    s->waits = 0;
    s->transmitting = s->waiting_fc = true;
    s->tx_deadline = now + TIMEOUT;
    p[0] = 0x10 | (size >> 8);
    p[1] = size;
    memcpy(p + 2, data, 6);
    return emit(s, p, 8, now);
}
void solar_isotp_receive(solar_isotp_t *s, const solar_can_frame_t *f, uint32_t now) {
    if (!solar_can_valid(f) || f->remote || f->id != s->rx_id || f->channel != s->channel ||
        f->extended != s->extended || !f->length)
        return;
    /* A late frame cannot revive a timed-out transaction. */
    solar_isotp_poll(s, now);
    const uint8_t *p = f->data;
    unsigned type = p[0] >> 4;
    if (type == 0) {
        unsigned n = p[0] & 15;
        if (!n || n > 7 || n + 1 > f->length) {
            bad_rx(s);
            return;
        }
        if (s->receiving) {
            bad_rx(s);
            return;
        }
        memcpy(s->rx, p + 1, n);
        s->rx_used = s->rx_size = n;
        s->complete = true;
    } else if (type == 1) {
        if (f->length != 8 || s->receiving) {
            bad_rx(s);
            return;
        }
        unsigned n = ((p[0] & 15) << 8) | p[1];
        s->complete = false;
        if (n <= 7 || n > SOLAR_ISOTP_MAX) {
            uint8_t fc[3] = {0x32, 0, 0};
            emit(s, fc, 3, now);
            bad_rx(s);
            return;
        }
        s->rx_size = n;
        s->rx_used = 6;
        memcpy(s->rx, p + 2, 6);
        s->rx_sn = 1;
        s->receiving = true;
        s->rx_deadline = now + TIMEOUT;
        uint8_t fc[3] = {0x30, 0, 1};
        emit(s, fc, 3, now); // bounded payload, unlimited block
    } else if (type == 2) {
        if (!s->receiving || (p[0] & 15) != s->rx_sn) {
            bad_rx(s);
            return;
        }
        unsigned n = s->rx_size - s->rx_used;
        if (n > 7)
            n = 7;
        if (f->length < n + 1) {
            bad_rx(s);
            return;
        }
        memcpy(s->rx + s->rx_used, p + 1, n);
        s->rx_used += n;
        s->rx_sn = (s->rx_sn + 1) & 15;
        s->rx_deadline = now + TIMEOUT;
        if (s->rx_used == s->rx_size) {
            s->receiving = false;
            s->complete = true;
        }
    } else if (type == 3) {
        if (!s->transmitting || !s->waiting_fc || f->length < 3) {
            ++s->errors;
            return;
        }
        unsigned status = p[0] & 15;
        if (status == 1 && ++s->waits <= 3) {
            s->tx_deadline = now + TIMEOUT;
            return;
        }
        if (status != 0 || (p[2] > 0x7f && (p[2] < 0xf1 || p[2] > 0xf9))) {
            ++s->errors;
            s->transmitting = false;
            return;
        }
        s->block_size = s->block_left = p[1];
        s->separation_ms = p[2] > 0x7f ? 1 : p[2];
        s->waiting_fc = false;
        s->next_tx = now + s->separation_ms;
    } else
        bad_rx(s);
}
void solar_isotp_poll(solar_isotp_t *s, uint32_t now) {
    if (s->receiving && due(now, s->rx_deadline)) {
        ++s->timeouts;
        s->receiving = false;
        s->complete = false;
    }
    if (!s->transmitting)
        return;
    if (s->waiting_fc) {
        if (due(now, s->tx_deadline)) {
            ++s->timeouts;
            s->transmitting = false;
        }
        return;
    }
    if (!due(now, s->next_tx))
        return;
    unsigned n = s->tx_size - s->tx_used;
    if (n > 7)
        n = 7;
    uint8_t p[8] = {0};
    p[0] = 0x20 | s->tx_sn;
    memcpy(p + 1, s->tx + s->tx_used, n);
    if (!emit(s, p, n + 1, now))
        return;
    s->tx_used += n;
    s->tx_sn = (s->tx_sn + 1) & 15;
    s->next_tx = now + s->separation_ms;
    if (s->tx_used == s->tx_size)
        s->transmitting = false;
    else if (s->block_size && !--s->block_left) {
        s->waiting_fc = true;
        s->tx_deadline = now + TIMEOUT;
    }
}
