#pragma once
#include "solar_os_scope_capture.h"
#define SOLAR_SCOPE_BASES 9U
typedef struct {
    unsigned timebase;
    uint16_t threshold;
    bool falling;
    uint32_t range_mv; /* Positive full-scale, matching the physical front-end jumper. */
} solar_scope_config_t;
typedef struct {
    size_t begin, count;
    bool triggered, clipped;
    int32_t min_mv, max_mv, mean_mv;
    uint32_t rms_mv, hz_milli;
} solar_scope_frame_t;
extern const uint32_t solar_scope_div_us[SOLAR_SCOPE_BASES];
extern const uint32_t solar_scope_rates[SOLAR_SCOPE_BASES];
int32_t solar_scope_mv(uint16_t raw, const solar_scope_config_t *config);
bool solar_scope_analyze(const uint16_t *, size_t, uint32_t,
                         const solar_scope_config_t *, solar_scope_frame_t *);
void solar_scope_demo(uint16_t *, size_t, uint32_t rate, unsigned waveform, uint32_t phase);
