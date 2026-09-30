#include "solar_os_scope.h"
#include "solar_os_scope_model.h"
#include "solar_os_gfx.h"
#include "solar_os_keys.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    solar_scope_config_t config;
    solar_scope_frame_t frame;
    uint16_t samples[SOLAR_SCOPE_SAMPLES], incoming[SOLAR_SCOPE_SAMPLES];
    uint32_t rate, frames, phase;
    bool demo, opened, armed, hold, single, normal, valid;
    unsigned wave;
    const char *notice;
} scope_app_t;
static void *scope_state;
#define app (*(scope_app_t *)scope_state)
static void volts(char *out, size_t n, int32_t mv) {
    unsigned magnitude = mv < 0 ? -mv : mv;
    snprintf(out, n, "%s%u.%03u", mv < 0 ? "-" : "", magnitude / 1000, magnitude % 1000);
}
static void render(solar_os_context_t *ctx) {
    solar_os_gfx_t *g = solar_os_context_gfx(ctx);
    int width = solar_os_gfx_width(g), height = solar_os_gfx_height(g);
    solar_os_gfx_clear(g, SOLAR_OS_GFX_COLOR_WHITE);
    solar_os_gfx_set_color(g, SOLAR_OS_GFX_COLOR_BLACK);
    solar_os_gfx_set_font(g, SOLAR_OS_GFX_FONT_MONO);
    char text[128], a[20], b[20], c[20], d[20], source[32];
    if (app.demo) snprintf(source, sizeof(source), "DEMO 100Hz");
    else snprintf(source, sizeof(source), "ADC pin%d 12-bit", solar_scope_capture_pin());
    snprintf(text, sizeof(text), "SCOPE CH1 %s | %s %s | %s", source,
             app.hold ? "HOLD" : "RUN", app.single ? "SINGLE" : app.normal ? "NORMAL" : "AUTO", app.notice);
    solar_os_gfx_text(g, 8, 17, text);
    snprintf(text, sizeof(text), "%lu us/div | %lu Sa/s | %s edge | %lu captures",
             (unsigned long)solar_scope_div_us[app.config.timebase], (unsigned long)app.rate,
             app.config.falling ? "falling" : "rising", (unsigned long)app.frames);
    solar_os_gfx_text(g, 8, 34, text);
    volts(a, sizeof(a), app.config.range_mv);
    volts(b, sizeof(b), app.config.range_mv / 8);
    volts(c, sizeof(c), solar_scope_mv(app.config.threshold, &app.config));
    snprintf(text, sizeof(text), "Range %s%sV | %s V/div | trigger %sV | %s",
             "0..", a, b, c,
             app.config.range_mv == 3300 ? "raw scaling" : "manual jumper scaling");
    solar_os_gfx_text(g, 8, 51, text);
    if (app.valid) {
        volts(a, sizeof(a), app.frame.min_mv); volts(b, sizeof(b), app.frame.max_mv);
        volts(c, sizeof(c), app.frame.max_mv - app.frame.min_mv);
        volts(d, sizeof(d), app.frame.mean_mv);
        snprintf(text, sizeof(text), "Min %sV  Max %sV  Vpp %sV  Mean %sV", a, b, c, d);
        solar_os_gfx_text(g, 8, 68, text);
        volts(a, sizeof(a), app.frame.rms_mv);
        if (app.frame.hz_milli)
            snprintf(text, sizeof(text), "RMS %sV  Freq ~%lu.%03luHz  %s", a,
                     (unsigned long)(app.frame.hz_milli / 1000), (unsigned long)(app.frame.hz_milli % 1000),
                     app.frame.clipped ? "ADC CLIP" : "uncalibrated");
        else snprintf(text, sizeof(text), "RMS %sV  Freq --  %s", a, app.frame.clipped ? "ADC CLIP" : "uncalibrated");
        solar_os_gfx_text(g, 8, 85, text);
    }
    int left = 12, top = 100, w = width - 24, h = height - 165;
    solar_os_gfx_set_color(g, SOLAR_OS_GFX_COLOR_LIGHT);
    for (int i = 0; i <= 10; ++i) solar_os_gfx_line(g, left + w*i/10, top, left + w*i/10, top+h);
    for (int i = 0; i <= 8; ++i) solar_os_gfx_line(g, left, top+h*i/8, left+w, top+h*i/8);
    solar_os_gfx_set_color(g, SOLAR_OS_GFX_COLOR_DARK);
    int ty = top + h - (int)((uint32_t)app.config.threshold * h / 4095);
    for (int x = left; x < left+w; x += 8) solar_os_gfx_line(g, x, ty, x+3, ty);
    if (app.valid) {
        solar_os_gfx_set_color(g, SOLAR_OS_GFX_COLOR_BLACK);
        /* Min/max per pixel preserves narrow sampled pulses when zoomed out. */
        int previous = 0;
        for (int x = 0; x < w; ++x) {
            size_t begin = (size_t)x * app.frame.count / w;
            size_t end = (size_t)(x+1) * app.frame.count / w;
            if (end <= begin) end = begin + 1;
            uint16_t lo = 4095, hi = 0;
            for (size_t i = begin; i < end; ++i) {
                uint16_t v = app.samples[app.frame.begin + i];
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            int y0 = top+h-(int)((uint32_t)hi*h/4095), y1 = top+h-(int)((uint32_t)lo*h/4095);
            solar_os_gfx_line(g, left+x, y0, left+x, y1);
            if (x) solar_os_gfx_line(g, left+x-1, previous, left+x, y0);
            previous = y1;
        }
        if (app.frame.triggered) solar_os_gfx_text(g, left+w/4-3, top-2, "T");
    }
    solar_os_gfx_set_color(g, SOLAR_OS_GFX_COLOR_BLACK);
    solar_os_gfx_text(g, 8, height-45, "Space run/hold  +/- time  Up/Down trigger  T edge  N auto/normal");
    solar_os_gfx_text(g, 8, height-28, "S single  R range  W demo wave  Q/Esc exit");
    solar_os_gfx_text(g, 8, height-11, "Snapshot gaps; no serial decode. Scaling must match front end.");
    solar_os_gfx_present(g);
}
static void cancel(void) {
    if (app.opened) solar_scope_capture_cancel();
    app.armed = false;
}
static esp_err_t start(solar_os_context_t *ctx) {
    if (!solar_os_context_gfx(ctx)) return ESP_ERR_INVALID_STATE;
    if (solar_os_gfx_width(solar_os_context_gfx(ctx)) < 320 ||
        solar_os_gfx_height(solar_os_context_gfx(ctx)) < 240) return ESP_ERR_NOT_SUPPORTED;
    if (ctx->argc == 2 && !strcmp(ctx->argv[1], "--demo")) app.demo = true;
    else if (ctx->argc != 1) {
        solar_os_context_finish(ctx, 2, "usage: scope [--demo]"); return ESP_OK;
    }
    if (!app.demo) {
        if (!solar_scope_capture_open()) {
            solar_os_context_finish(ctx, 2, "scope: ADC unavailable; assign a dedicated SK_SCOPE_ADC_PIN in board build. Use scope --demo.");
            return ESP_OK;
        }
        app.opened = true;
    }
    app.config = (solar_scope_config_t){.timebase=5, .threshold=2048, .range_mv=3300};
    app.rate = solar_scope_rates[app.config.timebase];
    app.notice = "waiting";
    solar_os_context_set_graphics_active(ctx, true);
    render(ctx); return ESP_OK;
}
static void stop(solar_os_context_t *ctx) {
    if (app.opened) solar_scope_capture_close();
    app.opened = app.armed = false;
    solar_os_context_set_graphics_active(ctx, false);
}
static void acquire(solar_os_context_t *ctx) {
    if (app.hold) return;
    uint32_t rate = solar_scope_rates[app.config.timebase];
    if (app.demo) {
        solar_scope_demo(app.incoming, SOLAR_SCOPE_SAMPLES, rate, app.wave, app.phase);
        app.phase += 17;
    } else {
        if (!app.armed) {
            if (!solar_scope_capture_arm(rate)) goto failed;
            app.armed = true;
            return;
        }
        int result = solar_scope_capture_take(app.incoming, SOLAR_SCOPE_SAMPLES, &rate);
        if (!result) return;
        app.armed = false;
        if (result < 0) goto failed;
    }
    solar_scope_frame_t frame;
    if (!solar_scope_analyze(app.incoming, SOLAR_SCOPE_SAMPLES, rate, &app.config, &frame)) goto failed;
    app.rate = rate;
    if (frame.triggered || (!app.normal && !app.single)) {
        memcpy(app.samples, app.incoming, sizeof(app.samples));
        app.frame = frame; app.valid = true; ++app.frames;
        app.notice = frame.triggered ? "triggered" : "free run";
        if (app.single) app.hold = true;
    } else app.notice = "waiting trigger";
    render(ctx); return;
failed:
    cancel(); app.hold = true; app.notice = "capture error"; render(ctx);
}
static bool event(solar_os_context_t *ctx, const solar_os_event_t *ev) {
    if (ev->type == SOLAR_OS_EVENT_TICK) { acquire(ctx); return true; }
    if (ev->type != SOLAR_OS_EVENT_CHAR) return false;
    unsigned char key = ev->data.ch;
    if (key == 'q' || key == 'Q' || key == SOLAR_OS_KEY_ESCAPE || key == SOLAR_OS_KEY_APP_EXIT) {
        solar_os_context_finish(ctx, 0, NULL); return true;
    }
    bool changed = false;
    switch (key) {
    case ' ': cancel(); app.hold = !app.hold; app.single = false; break;
    case 's': case 'S': cancel(); app.hold = false; app.single = true; break;
    case '+': case '=': if (app.config.timebase) { --app.config.timebase; changed = true; } break;
    case '-': if (app.config.timebase+1 < SOLAR_SCOPE_BASES) { ++app.config.timebase; changed = true; } break;
    case SOLAR_OS_KEY_UP: if (app.config.threshold < 3968) { app.config.threshold += 128; changed = true; } break;
    case SOLAR_OS_KEY_DOWN: if (app.config.threshold > 128) { app.config.threshold -= 128; changed = true; } break;
    case 't': case 'T': app.config.falling = !app.config.falling; changed = true; break;
    case 'n': case 'N': app.normal = !app.normal; app.single = false; break;
    case 'r': case 'R': app.config.range_mv = app.config.range_mv == 3300 ? 5000 : app.config.range_mv == 5000 ? 50000 : 3300; changed = true; break;
    case 'w': case 'W': if (app.demo) { app.wave = (app.wave+1)%3; changed = true; } break;
    default: return true;
    }
    if (changed) { cancel(); app.valid = false; app.rate = solar_scope_rates[app.config.timebase]; app.notice = app.hold ? "resume to capture" : "waiting"; }
    render(ctx); return true;
}
const solar_os_app_t solar_os_scope_app = {
    .name="scope", .summary="single-channel ADC oscilloscope",
    .app_class=SOLAR_OS_APP_CLASS_GUI, .start=start, .stop=stop, .event=event,
    .tick_interval_ms=100, .state_slot=&scope_state, .state_size=sizeof(scope_app_t),
    .state_storage=SOLAR_OS_APP_STATE_EXTERNAL_REQUIRED,
};
