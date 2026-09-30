#define _POSIX_C_SOURCE 200809L
#include "solar_os_keys.h"
#include "solar_os_obd_app.h"
#include "solar_os_tui_widgets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char screen[30][161];
static unsigned rows = 30, cols = 100;
size_t solar_os_tui_rows(const solar_os_tui_t *t) {
    (void)t;
    return rows;
}
size_t solar_os_tui_cols(const solar_os_tui_t *t) {
    (void)t;
    return cols;
}
esp_err_t solar_os_tui_screen_begin(solar_os_tui_t *t, solar_os_context_t *ctx) {
    (void)ctx;
    t->screen_active = true;
    return ESP_OK;
}
void solar_os_tui_end(solar_os_tui_t *t) { t->screen_active = false; }
void solar_os_tui_clear(solar_os_tui_t *t) {
    (void)t;
    memset(screen, 0, sizeof(screen));
}
void solar_os_tui_refresh(solar_os_tui_t *t) { (void)t; }
esp_err_t solar_os_tui_addstr(solar_os_tui_t *t, size_t row, size_t col, const char *text,
                              uint8_t attr) {
    (void)t;
    (void)attr;
    assert(row < rows && col == 0 && strlen(text) <= cols);
    strcpy(screen[row], text);
    return ESP_OK;
}
void solar_os_context_finish(solar_os_context_t *ctx, int code, const char *message) {
    ctx->exit_requested = true;
    ctx->exit_code = code;
    if (message)
        snprintf(ctx->status_message, sizeof(ctx->status_message), "%s", message);
}
static bool contains(const char *text) {
    for (unsigned i = 0; i < rows; ++i)
        if (strstr(screen[i], text))
            return true;
    return false;
}
static void key(solar_os_context_t *ctx, char ch) {
    solar_os_event_t e = {.type = SOLAR_OS_EVENT_CHAR, .data.ch = ch};
    assert(solar_os_obd_app.event(ctx, &e));
}
static void ticks(solar_os_context_t *ctx, uint32_t start, uint32_t end) {
    for (uint32_t t = start; t <= end; t += 10) {
        solar_os_event_t e = {.type = SOLAR_OS_EVENT_TICK, .data.tick_ms = t};
        solar_os_obd_app.event(ctx, &e);
    }
}
static void begin(solar_os_context_t *ctx) {
    *solar_os_obd_app.state_slot = calloc(1, solar_os_obd_app.state_size);
    assert(*solar_os_obd_app.state_slot);
    assert(solar_os_obd_app.start(ctx) == ESP_OK);
}
static void end(solar_os_context_t *ctx) {
    solar_os_obd_app.stop(ctx);
    free(*solar_os_obd_app.state_slot);
    *solar_os_obd_app.state_slot = NULL;
}
int main(void) {
    solar_os_context_t ctx = {.argc = 1};
    strcpy(ctx.argv[0], "obd");
    begin(&ctx);
    assert(ctx.exit_requested && ctx.exit_code == 2 &&
           strstr(ctx.status_message, "hardware driver not installed"));
    end(&ctx);
    char dir[] = "/tmp/solaros-obd-app-XXXXXX";
    assert(mkdtemp(dir));
    char path[160];
    snprintf(path, sizeof(path), "%s/report.txt", dir);
    ctx = (solar_os_context_t){.argc = 4};
    strcpy(ctx.argv[0], "obd");
    strcpy(ctx.argv[1], "--demo");
    strcpy(ctx.argv[2], "--report");
    strcpy(ctx.argv[3], path);
    begin(&ctx);
    assert(!ctx.exit_requested && contains("SIMULATED"));
    ticks(&ctx, 0, 6500);
    assert(contains("scan finished") && contains("P0300") && contains("P0420"));
    key(&ctx, 'c');
    assert(contains("Y confirms"));
    key(&ctx, 'n');
    ticks(&ctx, 6510, 7000);
    assert(contains("P0300"));
    key(&ctx, 'c');
    key(&ctx, 'y');
    assert(contains("Y confirms")); // lowercase not confirmation
    key(&ctx, 'Y');
    ticks(&ctx, 7010, 15000);
    assert(contains("No codes reported") && contains("acknowledged"));
    key(&ctx, '3');
    assert(contains("P0420"));
    key(&ctx, '\t');
    key(&ctx, '1');
    assert(contains("P0700"));
    key(&ctx, 's');
    assert(contains("Saved DEMO report"));
    FILE *f = fopen(path, "rb");
    assert(f);
    char saved[4096];
    size_t n = fread(saved, 1, sizeof(saved) - 1, f);
    saved[n] = 0;
    fclose(f);
    assert(strstr(saved, "DEMO") && strstr(saved, "P0700") && strstr(saved, "Permanent: P0420"));
    key(&ctx, 's');
    assert(contains("Cannot create report"));
    f = fopen(path, "rb");
    assert(f);
    char again[4096];
    size_t n2 = fread(again, 1, sizeof(again), f);
    fclose(f);
    assert(n == n2 && !memcmp(saved, again, n));
    // Bounds at smaller serial geometry, and clean exit while confirmation is open.
    cols = 40;
    rows = 12;
    key(&ctx, 'r');
    rows = 8;
    key(&ctx, '1');
    assert(contains("Resize terminal"));
    key(&ctx, 'q');
    assert(ctx.exit_requested && ctx.exit_code == 0);
    end(&ctx);
    assert(unlink(path) == 0 && rmdir(dir) == 0);
    rows = 30;
    cols = 100;
    ctx = (solar_os_context_t){.argc = 4};
    strcpy(ctx.argv[1], "--demo");
    strcpy(ctx.argv[2], "--scenario");
    strcpy(ctx.argv[3], "timeout");
    begin(&ctx);
    ticks(&ctx, 0, 6500);
    assert(contains("unavailable") && !contains("No codes reported"));
    key(&ctx, 'c');
    assert(!contains("Y confirms"));
    end(&ctx);
    puts("PASS: real obd app lifecycle, simulated scan, clear confirmation/cancel, rescan, save/no "
         "overwrite, geometry, timeout");
}
