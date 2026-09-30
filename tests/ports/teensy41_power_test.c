#include "solar_os_pd_power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void setup(solar_pd_power_t *p, solar_pd_power_demo_t *d, unsigned mode) {
    solar_pd_power_demo_init(d, (solar_pd_power_demo_mode_t)mode);
    solar_pd_power_init(p, &solar_pd_power_demo_backend, d, 15000, 2000);
    solar_pd_power_poll(p, 0);
}
static solar_pd_power_result_t bad_poll(void *u, uint32_t t, solar_pd_power_snapshot_t *s) {
    (void)u; (void)t; s->attached = true; s->count = 8;
    return SOLAR_PD_POWER_PENDING;
}
int main(void) {
    solar_pd_power_t p; solar_pd_power_demo_t d;
    setup(&p, &d, 0);
    assert(p.source.count == 4 && p.source.contract.mv == 5000);
    assert(!solar_pd_power_request(&p, 7, 1000, 0));
    assert(!solar_pd_power_request(&p, 3, 1000, 0)); // board voltage limit
    assert(!solar_pd_power_request(&p, 1, 2100, 0)); // board current limit
    assert(!solar_pd_power_request(&p, 1, 0, 0));
    assert(!solar_pd_power_request(&p, 1, 1001, 0));
    p.source.profiles[1].ma = 500;
    assert(!solar_pd_power_request(&p, 1, 1000, 0)); // source limit
    p.source.profiles[1].ma = 3000;
    assert(solar_pd_power_request(&p, 1, 1000, UINT32_MAX - 100));
    assert(!solar_pd_power_request(&p, 0, 1000, 0)); // busy
    solar_pd_power_poll(&p, 398); assert(p.state == SOLAR_PD_POWER_WAITING);
    solar_pd_power_poll(&p, 399); assert(p.state == SOLAR_PD_POWER_READY);
    assert(p.source.contract.mv == 9000 && p.source.contract.ma == 1000);
    d.source.attached = false;
    solar_pd_power_poll(&p, 400);
    assert(p.state == SOLAR_PD_POWER_DISCONNECTED && !p.source.contract_valid && !p.source.count);
    const solar_pd_power_state_t outcomes[] = {SOLAR_PD_POWER_READY, SOLAR_PD_POWER_REJECTED,
        SOLAR_PD_POWER_TIMEOUT, SOLAR_PD_POWER_DISCONNECTED, SOLAR_PD_POWER_IO_ERROR, SOLAR_PD_POWER_MISMATCH};
    for (unsigned mode = 0; mode < 6; ++mode) {
        setup(&p, &d, mode);
        assert(solar_pd_power_request(&p, 1, 1000, UINT32_MAX - 100));
        solar_pd_power_poll(&p, 2898);
        if (mode == 2) assert(p.state == SOLAR_PD_POWER_WAITING);
        solar_pd_power_poll(&p, 2899);
        assert(p.state == outcomes[mode]);
    }
    solar_pd_power_backend_t bad = {.poll = bad_poll};
    solar_pd_power_init(&p, &bad, NULL, 20000, 3000);
    solar_pd_power_poll(&p, 0);
    assert(p.state == SOLAR_PD_POWER_IO_ERROR && p.source.count == 0);
    solar_pd_power_init(&p, NULL, NULL, 0, 0);
    solar_pd_power_poll(&p, 0);
    assert(p.state == SOLAR_PD_POWER_IO_ERROR && !solar_pd_power_request(&p, 0, 1000, 0));
    puts("power policy/negotiation tests passed");
}
