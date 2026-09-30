#include "solar_os_scope_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    uint16_t s[SOLAR_SCOPE_SAMPLES];
    solar_scope_config_t c = {.timebase=5, .threshold=2048, .range_mv=5000};
    solar_scope_frame_t f;
    assert(solar_scope_mv(0, &c) == 0 && solar_scope_mv(4095, &c) == 5000);
    c.range_mv = 50000;
    assert(solar_scope_mv(4095, &c) == 50000 && solar_scope_mv(0, &c) == 0);
    c.range_mv = 3300;
    for (unsigned base = 0; base < SOLAR_SCOPE_BASES; ++base) {
        c.timebase = base;
        for (unsigned wave = 0; wave < 3; ++wave) {
            solar_scope_demo(s, SOLAR_SCOPE_SAMPLES, solar_scope_rates[base], wave, 37);
            assert(solar_scope_analyze(s, SOLAR_SCOPE_SAMPLES, solar_scope_rates[base], &c, &f));
            assert(f.begin + f.count <= SOLAR_SCOPE_SAMPLES && f.count >= 4);
            assert(!f.clipped && f.min_mv >= 0 && f.max_mv <= 3300);
            if (wave == 2) assert(!f.triggered && f.hz_milli == 0 && f.min_mv == f.max_mv);
            if (base >= 5 && wave != 2) {
                assert(f.hz_milli > 98000 && f.hz_milli < 102000);
                assert(f.triggered);
                assert(f.rms_mv > 1600 && f.rms_mv < 2100);
            }
        }
    }
    c.timebase = 5;
    solar_scope_demo(s, SOLAR_SCOPE_SAMPLES, 10000, 1, 0);
    assert(solar_scope_analyze(s, SOLAR_SCOPE_SAMPLES, 10000, &c, &f) && f.triggered);
    size_t rising = f.begin + f.count/4;
    assert(s[rising-1] < c.threshold && s[rising] > c.threshold);
    c.falling = true;
    assert(solar_scope_analyze(s, SOLAR_SCOPE_SAMPLES, 10000, &c, &f) && f.triggered);
    size_t falling = f.begin + f.count/4;
    assert(s[falling-1] > c.threshold && s[falling] < c.threshold);
    for (unsigned i=0; i<SOLAR_SCOPE_SAMPLES; ++i) s[i]=4095;
    assert(solar_scope_analyze(s, SOLAR_SCOPE_SAMPLES, 10000, &c, &f));
    assert(f.clipped && !f.triggered && f.rms_mv == 3300 && f.mean_mv == 3300);
    assert(!solar_scope_analyze(s, 0, 10000, &c, &f));
    assert(!solar_scope_analyze(s, SOLAR_SCOPE_SAMPLES, 0, &c, &f));
    c.timebase = SOLAR_SCOPE_BASES;
    assert(!solar_scope_analyze(s, SOLAR_SCOPE_SAMPLES, 10000, &c, &f));
    c.timebase = 5; s[0] = 5000;
    assert(!solar_scope_analyze(s, SOLAR_SCOPE_SAMPLES, 10000, &c, &f));
    puts("scope model: ranges, trigger edges, timebases, RMS, frequency, clipping passed");
}
