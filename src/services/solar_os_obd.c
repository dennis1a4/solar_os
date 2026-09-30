#include "solar_os_obd.h"
#include <string.h>
static const uint8_t services[] = {1, 3, 7, 10};
static bool emit(void *user, const solar_can_frame_t *f) {
    solar_obd_t *o = user;
    if (!solar_can_send(o->bus, f))
        return false;
    ++o->tx_frames;
    return true;
}
static void links(solar_obd_t *o) {
    for (unsigned i = 0; i < SOLAR_OBD_ECUS; ++i)
        solar_isotp_init(&o->links[i], 0x7e8 + i, 0x7e0 + i, o->channel, false, emit, o);
}
void solar_obd_init(solar_obd_t *o, solar_can_t *bus, uint8_t channel) {
    memset(o, 0, sizeof(*o));
    o->bus = bus;
    o->channel = channel;
    links(o);
}
static bool request(solar_obd_t *o, uint32_t now) {
    links(o);
    solar_can_frame_t f = {.id = o->clearing ? 0x7e0U + o->selected : 0x7df,
                           .timestamp_ms = now,
                           .channel = o->channel,
                           .length = 8};
    f.data[0] = o->clearing || o->phase ? 1 : 2;
    f.data[1] = o->clearing ? 4 : services[o->phase];
    f.data[2] = o->phase ? 0 : 1;
    o->deadline = now + 1500;
    o->active = true;
    if (emit(o, &f))
        return true;
    ++o->errors;
    o->active = false;
    o->done = true;
    return false;
}
bool solar_obd_scan(solar_obd_t *o, uint32_t now) {
    if (o->active)
        return false;
    // Preserve clear acknowledgements while replacing old scan data.
    for (unsigned i = 0; i < SOLAR_OBD_ECUS; ++i) {
        uint8_t result = o->ecus[i].clear_result, nrc = o->ecus[i].clear_nrc;
        memset(&o->ecus[i], 0, sizeof(o->ecus[i]));
        o->ecus[i].clear_result = result;
        o->ecus[i].clear_nrc = nrc;
    }
    o->phase = 0;
    o->done = false;
    o->clearing = false;
    return request(o, now);
}
bool solar_obd_clear(solar_obd_t *o, unsigned ecu, uint32_t now) {
    if (o->active || ecu >= SOLAR_OBD_ECUS || !o->ecus[ecu].seen)
        return false;
    o->selected = ecu;
    o->clearing = true;
    o->done = false;
    o->ecus[ecu].clear_result = o->ecus[ecu].clear_nrc = 0;
    return request(o, now);
}
void solar_obd_dtc(uint16_t code, char out[6]) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = "PCBU"[code >> 14];
    out[1] = '0' + ((code >> 12) & 3);
    out[2] = hex[(code >> 8) & 15];
    out[3] = hex[(code >> 4) & 15];
    out[4] = hex[code & 15];
    out[5] = 0;
}
bool solar_obd_decode(solar_obd_ecu_t *e, unsigned phase, const uint8_t *p, size_t n) {
    if (phase > 3 || !p || !n)
        return false;
    if (p[0] == 0x7f) {
        if (n != 3 || p[1] != services[phase])
            return false;
        e->seen = true;
        e->negative[phase] = p[2];
        return true;
    }
    if (p[0] != (services[phase] + 0x40))
        return false;
    if (!phase) {
        if (n != 6 || p[1] != 1)
            return false;
        memcpy(e->readiness, p + 2, 4);
    } else {
        if (n < 2 || p[1] > SOLAR_OBD_CODES || n != (size_t)(2 + 2 * p[1]))
            return false;
        for (unsigned i = 0; i < p[1]; ++i) {
            uint16_t code = ((uint16_t)p[2 + 2 * i] << 8) | p[3 + 2 * i];
            if (!code)
                return false;
        }
        e->count[phase - 1] = p[1];
        for (unsigned i = 0; i < p[1]; ++i)
            e->codes[phase - 1][i] = ((uint16_t)p[2 + 2 * i] << 8) | p[3 + 2 * i];
    }
    e->seen = true;
    e->valid |= 1U << phase;
    e->negative[phase] = 0;
    return true;
}
void solar_obd_poll(solar_obd_t *o, uint32_t now) {
    solar_can_frame_t f;
    while (solar_can_pop(&o->bus->rx, &f)) {
        ++o->rx_frames;
        if (!o->active || f.id < 0x7e8 || f.id > 0x7ef || f.extended || f.channel != o->channel ||
            (o->clearing && f.id != 0x7e8U + o->selected))
            continue;
        solar_isotp_receive(&o->links[f.id - 0x7e8], &f, now);
    }
    if (!o->active)
        return;
    for (unsigned i = 0; i < SOLAR_OBD_ECUS; ++i) {
        solar_isotp_t *s = &o->links[i];
        solar_isotp_poll(s, now);
        o->errors += s->errors;
        o->timeouts += s->timeouts;
        s->errors = s->timeouts = 0;
        if (!s->complete)
            continue;
        s->complete = false;
        if (o->clearing) {
            if (s->rx_size == 1 && s->rx[0] == 0x44)
                o->ecus[i].clear_result = 1;
            else if (s->rx_size == 3 && s->rx[0] == 0x7f && s->rx[1] == 4) {
                o->ecus[i].clear_result = 3;
                o->ecus[i].clear_nrc = s->rx[2];
            } else
                ++o->errors;
        } else if (!solar_obd_decode(&o->ecus[i], o->phase, s->rx, s->rx_size))
            ++o->errors;
    }
    if ((int32_t)(now - o->deadline) < 0)
        return;
    o->active = false;
    if (o->clearing) {
        if (!o->ecus[o->selected].clear_result) {
            o->ecus[o->selected].clear_result = 2;
            ++o->timeouts;
        }
        solar_obd_scan(o, now); // verify actual state even after rejected/unknown outcome
    } else {
        bool response = false;
        for (unsigned i = 0; i < SOLAR_OBD_ECUS; ++i)
            response |= (o->ecus[i].valid & (1U << o->phase)) || o->ecus[i].negative[o->phase];
        if (!response)
            ++o->timeouts;
        if (++o->phase < 4)
            request(o, now);
        else
            o->done = true;
    }
}
bool solar_obd_report(const solar_obd_t *o, FILE *out, const char *scenario) {
    fprintf(out,
            "SolarOS OBD DEMO - simulated ECUs; no vehicle connected\nScenario: %s\nScan: %s\n",
            scenario, o->done ? "finished" : "in progress");
    for (unsigned i = 0; i < SOLAR_OBD_ECUS; ++i) {
        const solar_obd_ecu_t *e = &o->ecus[i];
        if (!e->seen && !e->clear_result)
            continue;
        fprintf(out, "ECU %03X valid=%02X clear=%u NRC=%02X\n", 0x7e8 + i, e->valid,
                e->clear_result, e->clear_nrc);
        if (e->valid & 1)
            fprintf(out, "MIL=%s reported stored=%u readiness=%02X %02X %02X %02X\n",
                    e->readiness[0] & 128 ? "on" : "off", e->readiness[0] & 127, e->readiness[0],
                    e->readiness[1], e->readiness[2], e->readiness[3]);
        for (unsigned k = 0; k < 3; ++k) {
            fprintf(out, "%s: ", k == 0 ? "Stored" : k == 1 ? "Pending" : "Permanent");
            if (!(e->valid & (2U << k)))
                fprintf(out, "unavailable (NRC=%02X)", e->negative[k + 1]);
            else if (!e->count[k])
                fprintf(out, "none");
            else
                for (unsigned j = 0; j < e->count[k]; ++j) {
                    char code[6];
                    solar_obd_dtc(e->codes[k][j], code);
                    fprintf(out, "%s ", code);
                }
            fputc('\n', out);
        }
    }
    fprintf(out, "RX=%lu TX=%lu errors=%lu timeouts=%lu RX-drops=%lu TX-drops=%lu\n",
            (unsigned long)o->rx_frames, (unsigned long)o->tx_frames, (unsigned long)o->errors,
            (unsigned long)o->timeouts, (unsigned long)o->bus->rx.dropped,
            (unsigned long)o->bus->tx.dropped);
    fprintf(out, "Clear result: 0=not requested, 1=acknowledged, 2=no response, "
                 "3=rejected.\nPermanent codes cannot be cleared by a scan tool.\n");
    return !ferror(out);
}
