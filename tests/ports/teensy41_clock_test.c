#define _GNU_SOURCE
#include "solar_os_clock.h"
#include "solar_os_gfx.h"
#include "solar_os_keys.h"
#include "solar_os_schedule.h"
#include "solar_os_time.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t monotonic_ms;
static uint32_t rtc_epoch;
static unsigned tones, stops, presents;
static uint32_t draw_hash, last_hash;
static bool fail_replace;
static solar_os_gfx_color_t color;
int64_t esp_timer_get_time(void) { return (int64_t)monotonic_ms * 1000; }
uint32_t sk_clock_rtc_epoch(void) { return rtc_epoch; }
void sk_clock_rtc_set(uint32_t value) { rtc_epoch=value; }
void sk_clock_alarm_sound(bool on) {
    if (on)
        ++tones;
    else
        ++stops;
}
bool solar_os_storage_flash_is_mounted(void) { return true; }
esp_err_t solar_os_storage_sync_file(FILE *f) { return fflush(f) == 0 ? ESP_OK : ESP_FAIL; }
esp_err_t sk_settings_replace(const char *a, const char *b) {
    return !fail_replace && rename(a, b) == 0 ? ESP_OK : ESP_FAIL;
}
size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t n = strlen(src);
    if (size) {
        size_t k = n < size - 1 ? n : size - 1;
        memcpy(dst, src, k);
        dst[k] = 0;
    }
    return n;
}
solar_os_gfx_t *solar_os_context_gfx(solar_os_context_t *ctx) { return ctx->gfx; }
int solar_os_context_argc(const solar_os_context_t *ctx) { return ctx->argc; }
const char *solar_os_context_argv(const solar_os_context_t *ctx, int i) { return ctx->argv[i]; }
void solar_os_context_set_graphics_active(solar_os_context_t *ctx, bool active) {
    ctx->graphics_active = active;
}
void solar_os_context_finish(solar_os_context_t *ctx, int code, const char *s) {
    (void)s;
    ctx->exit_requested = true;
    ctx->exit_code = code;
}
size_t solar_os_gfx_width(const solar_os_gfx_t *g) {
    (void)g;
    return 800;
}
size_t solar_os_gfx_height(const solar_os_gfx_t *g) {
    (void)g;
    return 480;
}
static void hash(unsigned value) { draw_hash = (draw_hash ^ value) * 16777619U; }
void solar_os_gfx_clear(solar_os_gfx_t *g, solar_os_gfx_color_t c) {
    (void)g;
    draw_hash = 2166136261U;
    hash(c);
}
void solar_os_gfx_set_color(solar_os_gfx_t *g, solar_os_gfx_color_t c) {
    (void)g;
    color = c;
}
void solar_os_gfx_fill_polygon(solar_os_gfx_t *g, const solar_os_gfx_point_t *p, size_t n) {
    (void)g;
    hash(color);
    hash(n);
    for (size_t i = 0; i < n; ++i) {
        assert(p[i].x >= 0 && p[i].x < 800 && p[i].y >= 0 && p[i].y < 480);
        hash(p[i].x);
        hash(p[i].y);
    }
}
void solar_os_gfx_fill_circle(solar_os_gfx_t *g, int x, int y, int r) {
    (void)g;
    assert(x - r >= 0 && x + r < 800 && y - r >= 0 && y + r < 480);
    hash(color);
    hash(x);
    hash(y);
    hash(r);
}
void solar_os_gfx_present(solar_os_gfx_t *g) {
    (void)g;
    last_hash = draw_hash;
    ++presents;
}
static void begin(solar_os_context_t *ctx) {
    *solar_os_clock_app.state_slot = calloc(1, solar_os_clock_app.state_size);
    assert(*solar_os_clock_app.state_slot);
    assert(solar_os_clock_app.start(ctx) == ESP_OK);
}
static void end(solar_os_context_t *ctx) {
    solar_os_clock_app.stop(ctx);
    free(*solar_os_clock_app.state_slot);
    *solar_os_clock_app.state_slot = NULL;
    assert(!ctx->graphics_active);
}
static void tick(solar_os_context_t *ctx, uint64_t now) {
    monotonic_ms = now;
    solar_os_schedule_poll();
    solar_os_event_t e = {.type = SOLAR_OS_EVENT_TICK, .data.tick_ms = (uint32_t)now};
    solar_os_clock_app.event(ctx, &e);
}
static void key(solar_os_context_t *ctx, char c) {
    solar_os_event_t e = {.type = SOLAR_OS_EVENT_CHAR, .data.ch = c};
    solar_os_clock_app.event(ctx, &e);
}
static uint32_t epoch(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute) {
    struct tm t = {.tm_year = (int)year - 1900,
                   .tm_mon = (int)month - 1,
                   .tm_mday = (int)day,
                   .tm_hour = (int)hour,
                   .tm_min = (int)minute};
    return (uint32_t)timegm(&t);
}
int main(int argc, char **argv) {
    assert(argc == 3 && chdir(argv[1]) == 0);
    solar_os_datetime_t d;
    char name[32], posix[80];
    rtc_epoch = epoch(2026, 1, 15, 12, 34);
    if (!strcmp(argv[2], "reload")) {
        solar_os_time_get_timezone(name, sizeof(name), posix, sizeof(posix));
        assert(!strcmp(name, "Manitoba"));
        assert(solar_os_time_get_datetime(&d) == ESP_OK && d.hour == 7 && d.minute == 34);
        puts("PASS: timezone persists across independent process restart");
        return 0;
    }
    solar_os_time_get_timezone(name, sizeof(name), posix, sizeof(posix));
    assert(!strcmp(name, "UTC"));
    assert(solar_os_time_get_datetime(&d) == ESP_OK && d.hour == 12 && d.minute == 34);
    assert(solar_os_time_set_timezone("UTC+2") == ESP_OK);
    assert(solar_os_time_get_datetime(&d) == ESP_OK && d.hour == 14);
    assert(solar_os_time_set_timezone("CST6CDT,M3.2.0,M11.1.0") == ESP_OK);
    assert(solar_os_time_get_datetime(&d) == ESP_OK && d.hour == 6);
    rtc_epoch = epoch(2026, 7, 15, 12, 34);
    assert(solar_os_time_get_datetime(&d) == ESP_OK && d.hour == 7);
    rtc_epoch = epoch(2026, 3, 8, 7, 59);
    assert(solar_os_time_get_datetime(&d) == ESP_OK && d.hour == 1 && d.minute == 59);
    rtc_epoch = epoch(2026, 3, 8, 8, 0);
    assert(solar_os_time_get_datetime(&d) == ESP_OK && d.hour == 3 && d.minute == 0);
    assert(solar_os_time_set_timezone("Manitoba") == ESP_OK);
    rtc_epoch = epoch(2027, 1, 15, 12, 34);
    assert(solar_os_time_get_datetime(&d) == ESP_OK && d.hour == 7);
    rtc_epoch = epoch(2027, 7, 15, 12, 34);
    assert(solar_os_time_get_datetime(&d) == ESP_OK && d.hour == 7);
    solar_os_time_get_timezone(name, sizeof(name), posix, sizeof(posix));
    assert(!strcmp(name, "Manitoba") && !strcmp(posix, "UTC5"));
    fail_replace = true;
    assert(solar_os_time_set_timezone("UTC+9") != ESP_OK);
    fail_replace = false;
    solar_os_time_get_timezone(name, sizeof(name), posix, sizeof(posix));
    assert(!strcmp(name, "Manitoba"));
    assert(solar_os_time_set_timezone("UTC+99") != ESP_OK);
    d = (solar_os_datetime_t){.year = 2024, .month = 2, .day = 29};
    assert(solar_os_time_datetime_is_valid(&d));
    assert(solar_os_time_set_datetime(&d) == ESP_OK);
    assert(rtc_epoch == epoch(2024, 2, 29, 5, 0)); /* local midnight, UTC-5 */
    d.year = 2100;
    assert(!solar_os_time_datetime_is_valid(&d));
    uint32_t saved_epoch = rtc_epoch;
    assert(solar_os_time_set_datetime(&d) == ESP_ERR_INVALID_ARG && rtc_epoch == saved_epoch);
    d = (solar_os_datetime_t){.year=2107, .month=1, .day=1};
    assert(solar_os_time_set_datetime(&d) == ESP_ERR_INVALID_ARG && rtc_epoch == saved_epoch);
    assert(solar_os_time_set_timezone("CST6CDT,M3.2.0,M11.1.0") == ESP_OK);
    d = (solar_os_datetime_t){.year=2026, .month=3, .day=8, .hour=2, .minute=30};
    assert(solar_os_time_set_datetime(&d) == ESP_ERR_INVALID_ARG && rtc_epoch == saved_epoch);
    d.hour=3;
    assert(solar_os_time_set_datetime(&d) == ESP_OK && rtc_epoch == epoch(2026,3,8,8,30));
    assert(solar_os_time_set_timezone("Manitoba") == ESP_OK);
    rtc_epoch = 0;
    assert(solar_os_time_get_datetime(&d) == ESP_ERR_INVALID_STATE && !d.clock_integrity);
    rtc_epoch = epoch(2026, 7, 15, 12, 34);
    solar_os_context_t ctx = {.argc = 1, .gfx = (solar_os_gfx_t *)(uintptr_t)1};
    strcpy(ctx.argv[0], "clock");
    begin(&ctx);
    uint32_t time_hash = last_hash;
    assert(ctx.graphics_active);
    rtc_epoch = 0;
    tick(&ctx, 1000);
    assert(last_hash != time_hash);
    solar_os_clock_app.suspend(&ctx);
    unsigned before = presents;
    tick(&ctx, 2000);
    assert(presents == before && !ctx.graphics_active);
    solar_os_clock_app.resume(&ctx);
    assert(presents > before && ctx.graphics_active);
    end(&ctx);
    ctx = (solar_os_context_t){.argc = 2, .gfx = (solar_os_gfx_t *)(uintptr_t)1};
    strcpy(ctx.argv[1], "-s");
    monotonic_ms = 0xfffffff0ULL;
    begin(&ctx);
    uint32_t zero = last_hash;
    key(&ctx, ' ');
    tick(&ctx, monotonic_ms + 1500);
    assert(last_hash != zero);
    key(&ctx, ' ');
    uint32_t paused = last_hash;
    tick(&ctx, monotonic_ms + 5000);
    assert(last_hash == paused);
    key(&ctx, 'r');
    assert(last_hash == zero);
    key(&ctx, 27);
    assert(ctx.exit_requested);
    end(&ctx);
    ctx = (solar_os_context_t){.argc = 3, .gfx = (solar_os_gfx_t *)(uintptr_t)1};
    strcpy(ctx.argv[1], "-a");
    strcpy(ctx.argv[2], "00:02");
    monotonic_ms = 10000;
    begin(&ctx);
    uint32_t remaining;
    assert(solar_os_schedule_remaining_seconds("_clock", &remaining) == ESP_OK && remaining == 2);
    assert(solar_os_schedule_add_relative("_clock", 2, SOLAR_OS_SCHEDULE_ACTION_ALARM, NULL,
                                          false) == ESP_ERR_INVALID_STATE);
    tick(&ctx, 11001);
    assert(solar_os_schedule_remaining_seconds("_clock", &remaining) == ESP_OK && remaining == 1 &&
           tones == 0);
    solar_os_clock_app.suspend(&ctx);
    tick(&ctx, 12000);
    assert(tones == 1 && solar_os_schedule_alarm_active(NULL, 0));
    tick(&ctx, 12500);
    assert(tones == 1);
    tick(&ctx, 13600);
    assert(tones == 2);
    solar_os_clock_app.resume(&ctx);
    end(&ctx);
    assert(stops > 0 && !solar_os_schedule_alarm_active(NULL, 0));
    solar_os_schedule_poll();
    assert(tones == 2);
    assert(solar_os_schedule_remaining_seconds("_clock", &remaining) == ESP_ERR_NOT_FOUND);
    assert(solar_os_schedule_add_relative("other", 1, SOLAR_OS_SCHEDULE_ACTION_ALARM, NULL,
                                          false) == ESP_ERR_INVALID_ARG);
    assert(solar_os_schedule_add_relative("_clock", 1, SOLAR_OS_SCHEDULE_ACTION_ALARM, NULL,
                                          true) == ESP_ERR_INVALID_ARG);
    *solar_os_clock_app.state_slot = calloc(1, solar_os_clock_app.state_size);
    strcpy(ctx.argv[2], "00:00");
    assert(solar_os_clock_app.start(&ctx) == ESP_ERR_INVALID_ARG);
    strcpy(ctx.argv[2], "00:60");
    assert(solar_os_clock_app.start(&ctx) == ESP_ERR_INVALID_ARG);
    strcpy(ctx.argv[2], "100:00");
    assert(solar_os_clock_app.start(&ctx) == ESP_ERR_INVALID_ARG);
    ctx.gfx = NULL;
    assert(solar_os_clock_app.start(&ctx) == ESP_ERR_INVALID_STATE);
    free(*solar_os_clock_app.state_slot);
    *solar_os_clock_app.state_slot = NULL;
    puts("PASS: upstream clock rendering/lifecycle, stopwatch wrap/pause/reset, suspended "
         "countdown/cleanup, RTC validity, timezone/DST/save rollback");
}
