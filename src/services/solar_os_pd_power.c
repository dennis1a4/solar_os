#include "solar_os_pd_power.h"
#include <string.h>

void solar_pd_power_init(solar_pd_power_t *p, const solar_pd_power_backend_t *b, void *user,
                      uint16_t mv, uint16_t ma) {
    memset(p, 0, sizeof(*p));
    p->backend = b; p->user = user; p->max_mv = mv; p->max_ma = ma;
}
const char *solar_pd_power_state_name(solar_pd_power_state_t state) {
    static const char *const names[] = {"idle", "negotiating", "contract confirmed",
        "request rejected", "negotiation timed out", "source disconnected",
        "controller communication error", "contract mismatch"};
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "unknown";
}
void solar_pd_power_poll(solar_pd_power_t *p, uint32_t now) {
    if (!p->backend || !p->backend->poll) { p->state = SOLAR_PD_POWER_IO_ERROR; return; }
    solar_pd_power_result_t result = p->backend->poll(p->user, now, &p->source);
    if (p->source.count > SOLAR_PD_POWER_PROFILES || result == SOLAR_PD_POWER_FAILED) {
        memset(&p->source, 0, sizeof(p->source));
        p->state = SOLAR_PD_POWER_IO_ERROR;
    } else if (!p->source.attached || result == SOLAR_PD_POWER_DETACHED) {
        memset(&p->source, 0, sizeof(p->source));
        p->state = SOLAR_PD_POWER_DISCONNECTED;
    } else if (p->state == SOLAR_PD_POWER_WAITING) {
        if (result == SOLAR_PD_POWER_DENIED) p->state = SOLAR_PD_POWER_REJECTED;
        else if (result == SOLAR_PD_POWER_ACCEPTED) {
            p->state = p->source.contract_valid &&
                p->source.contract.mv == p->requested.mv &&
                p->source.contract.ma == p->requested.ma ? SOLAR_PD_POWER_READY : SOLAR_PD_POWER_MISMATCH;
        } else if ((uint32_t)(now - p->began) >= 3000) p->state = SOLAR_PD_POWER_TIMEOUT;
        if (p->state != SOLAR_PD_POWER_WAITING && p->state != SOLAR_PD_POWER_READY)
            p->source.contract_valid = false;
    } else if (p->state == SOLAR_PD_POWER_READY &&
               (!p->source.contract_valid || p->source.contract.mv != p->requested.mv ||
                p->source.contract.ma != p->requested.ma)) {
        p->state = SOLAR_PD_POWER_MISMATCH;
    }
}
bool solar_pd_power_request(solar_pd_power_t *p, unsigned index, uint16_t ma, uint32_t now) {
    if (!p->backend || !p->backend->request || !p->source.attached ||
        p->source.count > SOLAR_PD_POWER_PROFILES || index >= p->source.count ||
        p->state == SOLAR_PD_POWER_WAITING) return false;
    solar_pd_power_profile_t request = p->source.profiles[index];
    if (request.mv < 5000 || request.mv > 20000 || request.mv > p->max_mv ||
        request.mv % 50 || !ma || ma % 10 || ma > 5000 ||
        ma > request.ma || ma > p->max_ma) return false;
    request.ma = ma;
    p->requested = request;
    p->source.contract_valid = false;
    if (!p->backend->request(p->user, request, now)) {
        p->state = SOLAR_PD_POWER_IO_ERROR;
        return false;
    }
    p->began = now;
    p->state = SOLAR_PD_POWER_WAITING;
    return true;
}

void solar_pd_power_demo_init(solar_pd_power_demo_t *d, solar_pd_power_demo_mode_t mode) {
    memset(d, 0, sizeof(*d));
    d->mode = mode;
    d->source.attached = d->source.contract_valid = true;
    d->source.count = 4;
    const uint16_t volts[] = {5000, 9000, 15000, 20000};
    for (unsigned i = 0; i < 4; ++i)
        d->source.profiles[i] = (solar_pd_power_profile_t){volts[i], 3000};
    d->source.contract = (solar_pd_power_profile_t){5000, 1000};
}
static bool demo_request(void *user, solar_pd_power_profile_t request, uint32_t now) {
    solar_pd_power_demo_t *d = user;
    if (!d->source.attached) return false;
    d->request = request; d->began = now; d->pending = true;
    d->source.contract_valid = false;
    return true;
}
static solar_pd_power_result_t demo_poll(void *user, uint32_t now, solar_pd_power_snapshot_t *s) {
    solar_pd_power_demo_t *d = user;
    solar_pd_power_result_t result = SOLAR_PD_POWER_PENDING;
    if (d->pending && (uint32_t)(now - d->began) >= 500 && d->mode != SOLAR_PD_POWER_DEMO_TIMEOUT) {
        d->pending = false;
        switch (d->mode) {
        case SOLAR_PD_POWER_DEMO_NORMAL:
        case SOLAR_PD_POWER_DEMO_MISMATCH:
            d->source.contract = d->request;
            if (d->mode == SOLAR_PD_POWER_DEMO_MISMATCH) d->source.contract.mv = 5000;
            d->source.contract_valid = true;
            result = SOLAR_PD_POWER_ACCEPTED;
            break;
        case SOLAR_PD_POWER_DEMO_REJECT: result = SOLAR_PD_POWER_DENIED; break;
        case SOLAR_PD_POWER_DEMO_DISCONNECT:
            d->source.attached = false; result = SOLAR_PD_POWER_DETACHED; break;
        case SOLAR_PD_POWER_DEMO_IO_ERROR: result = SOLAR_PD_POWER_FAILED; break;
        default: break;
        }
    }
    *s = d->source;
    return result;
}
const solar_pd_power_backend_t solar_pd_power_demo_backend = {demo_request, demo_poll};
