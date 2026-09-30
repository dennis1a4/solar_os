#include "solar_os_scope_model.h"
#include <math.h>
#include <string.h>
const uint32_t solar_scope_div_us[SOLAR_SCOPE_BASES] = {100,200,500,1000,2000,5000,10000,20000,50000};
const uint32_t solar_scope_rates[SOLAR_SCOPE_BASES] = {100000,100000,100000,50000,25000,10000,5000,2500,1000};
int32_t solar_scope_mv(uint16_t raw, const solar_scope_config_t *c) {
    if (raw > 4095) raw = 4095;
    uint32_t span = c->range_mv;
    return (int32_t)(((uint64_t)raw * span + 2047) / 4095);
}
bool solar_scope_analyze(const uint16_t *s, size_t n, uint32_t rate,
                         const solar_scope_config_t *c, solar_scope_frame_t *f) {
    memset(f, 0, sizeof(*f));
    if (!s || n < 4 || n > SOLAR_SCOPE_SAMPLES || !rate ||
        c->timebase >= SOLAR_SCOPE_BASES || c->threshold > 4095 ||
        !c->range_mv || c->range_mv > 50000) return false;
    size_t window = (uint64_t)solar_scope_div_us[c->timebase] * 10 * rate / 1000000;
    if (window < 4 || window > n) return false;
    size_t pre = window / 4;
    bool armed = false;
    unsigned low = c->threshold > 16 ? c->threshold - 16 : 0;
    unsigned high = c->threshold < 4079 ? c->threshold + 16 : 4095;
    for (size_t i = pre; i <= n - window + pre; ++i) {
        if (c->falling ? s[i] >= high : s[i] <= low) armed = true;
        if (armed && (c->falling ? s[i] < c->threshold : s[i] > c->threshold)) {
            f->begin = i - pre; f->triggered = true; break;
        }
    }
    f->count = window;
    uint16_t min = 4095, max = 0;
    int64_t sum = 0;
    uint64_t squares = 0;
    for (size_t i = 0; i < window; ++i) {
        uint16_t v = s[f->begin + i];
        if (v > 4095) return false;
        if (v < min) min = v;
        if (v > max) max = v;
        if (v <= 2 || v >= 4093) f->clipped = true;
        int32_t mv = solar_scope_mv(v, c);
        sum += mv; squares += (int64_t)mv * mv;
    }
    f->min_mv = solar_scope_mv(min, c); f->max_mv = solar_scope_mv(max, c);
    f->mean_mv = sum / (int64_t)window;
    f->rms_mv = (uint32_t)sqrt((double)squares / window);
    /* Frequency estimate needs >=3 rising crossings and measurable amplitude.
       This is a periodic-signal estimate, not serial decoding or alias detection. */
    if (max - min >= 64) {
        unsigned mid = ((unsigned)min + max) / 2;
        unsigned hysteresis = (max - min) / 10;
        size_t first = 0, last = 0, crossings = 0;
        armed = false;
        for (size_t i = 0; i < window; ++i) {
            unsigned v = s[f->begin + i];
            if (v < mid - hysteresis) armed = true;
            if (armed && v >= mid) {
                if (!crossings) first = i;
                last = i; ++crossings; armed = false;
            }
        }
        if (crossings >= 3 && last > first)
            f->hz_milli = (uint64_t)rate * 1000 * (crossings - 1) / (last - first);
    }
    return true;
}
void solar_scope_demo(uint16_t *s, size_t n, uint32_t rate, unsigned wave, uint32_t phase) {
    /* 100 Hz remains below Nyquist even on the slowest demonstration timebase. */
    for (size_t i = 0; i < n; ++i) {
        double t = (double)(i + phase) * 100 / rate;
        double v = wave == 1 ? (fmod(t, 1.0) < .5 ? 1 : -1) :
                   wave == 2 ? 0 : sin(t * 6.283185307179586);
        s[i] = (uint16_t)(2048 + 1400 * v);
    }
}
