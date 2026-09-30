#include "solar_os_obd_demo.h"
#include <string.h>
static bool reply(void *user, const solar_can_frame_t *frame) {
    solar_obd_demo_t *d = user;
    solar_can_frame_t f = *frame;
    if (d->mode == OBD_DEMO_SEQUENCE && (f.data[0] >> 4) == 2)
        f.data[0] ^= 1;
    return solar_can_push(&d->bus->rx, &f);
}
void solar_obd_demo_init(solar_obd_demo_t *d, solar_can_t *bus, solar_obd_demo_mode_t mode) {
    memset(d, 0, sizeof(*d));
    d->bus = bus;
    d->mode = mode;
    for (unsigned i = 0; i < 2; ++i)
        solar_isotp_init(&d->ecu[i], 0x7e0 + i, 0x7e8 + i, 0, false, reply, d);
}
static void respond(solar_obd_demo_t *d, unsigned i, uint32_t now) {
    solar_isotp_t *s = &d->ecu[i];
    s->complete = false;
    uint8_t p[16] = {0};
    size_t n = 0;
    uint8_t service = s->rx[0];
    if (service == 1 && s->rx_size == 2 && s->rx[1] == 1) {
        p[0] = 0x41;
        p[1] = 1;
        p[2] = d->cleared[i] ? 0 : (0x80 | (i ? 1 : 4));
        p[3] = d->cleared[i] ? 0x77 : 7;
        p[4] = 0x65;
        p[5] = d->cleared[i] ? 0x65 : 4;
        n = 6;
    } else if ((service == 3 || service == 7 || service == 10) && s->rx_size == 1) {
        static const uint16_t stored[2][4] = {{0x0300, 0x0301, 0x0171, 0x0420}, {0x0700, 0, 0, 0}};
        p[0] = service + 0x40;
        unsigned count = service == 10   ? (i ? 0 : 1)
                         : d->cleared[i] ? 0
                         : service == 7  ? 1
                         : i             ? 1
                                         : 4;
        p[1] = count;
        n = 2 + 2 * count;
        for (unsigned j = 0; j < count; ++j) {
            uint16_t code = service == 3    ? stored[i][j]
                            : service == 10 ? 0x0420
                            : i             ? 0x0715
                                            : 0x0302;
            p[2 + 2 * j] = code >> 8;
            p[3 + 2 * j] = code;
        }
    } else if (service == 4 && s->rx_size == 1) {
        if (d->mode == OBD_DEMO_REJECT) {
            p[0] = 0x7f;
            p[1] = 4;
            p[2] = 0x22;
            n = 3;
        } else {
            d->cleared[i] = true;
            p[0] = 0x44;
            n = 1;
        }
    } else {
        p[0] = 0x7f;
        p[1] = service;
        p[2] = 0x11;
        n = 3;
    }
    solar_isotp_send(s, p, n, now);
}
void solar_obd_demo_poll(solar_obd_demo_t *d, uint32_t now) {
    solar_can_frame_t f;
    while (solar_can_pop(&d->bus->tx, &f)) {
        if (d->mode == OBD_DEMO_TIMEOUT)
            continue;
        for (unsigned i = 0; i < 2; ++i) {
            solar_can_frame_t routed = f;
            if (f.id == 0x7df)
                routed.id = 0x7e0 + i;
            solar_isotp_receive(&d->ecu[i], &routed, now);
            if (d->ecu[i].complete)
                respond(d, i, now);
        }
    }
    for (unsigned i = 0; i < 2; ++i)
        solar_isotp_poll(&d->ecu[i], now);
}
