#define _POSIX_C_SOURCE 200809L
#include "solar_os_keys.h"
#include "solar_os_power_app.h"
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
    assert(solar_os_power_app.event(ctx, &e));
}
static void tick(solar_os_context_t *ctx, uint32_t now) {
    solar_os_event_t e = {.type = SOLAR_OS_EVENT_TICK, .data.tick_ms = now};
    assert(solar_os_power_app.event(ctx, &e));
}
static void begin(solar_os_context_t *ctx) {
    *solar_os_power_app.state_slot = calloc(1, solar_os_power_app.state_size);
    assert(*solar_os_power_app.state_slot);
    assert(solar_os_power_app.start(ctx) == ESP_OK);
}
static void end(solar_os_context_t *ctx) {
    solar_os_power_app.stop(ctx);
    free(*solar_os_power_app.state_slot);
    *solar_os_power_app.state_slot = NULL;
}
int main(void) {
    solar_os_context_t ctx = {.argc = 1};
    strcpy(ctx.argv[0], "pdpower");
    begin(&ctx);
    assert(ctx.exit_requested && ctx.exit_code == 2 && strstr(ctx.status_message, "not configured"));
    end(&ctx);
    const char *modes[] = {"normal", "reject", "timeout", "disconnect", "io-error", "mismatch"};
    const char *outcomes[] = {"contract confirmed", "request rejected", "timed out", "disconnected", "communication error", "contract mismatch"};
    for (unsigned i = 0; i < 6; ++i) {
        ctx = (solar_os_context_t){.argc = 4};
        strcpy(ctx.argv[0], "pdpower"); strcpy(ctx.argv[1], "--demo");
        strcpy(ctx.argv[2], "--scenario"); strcpy(ctx.argv[3], modes[i]);
        begin(&ctx);
        assert(!ctx.exit_requested && contains("SIMULATION ONLY") && contains("5000 mV"));
        key(&ctx, '2'); key(&ctx, '+'); key(&ctx, '\r');
        assert(contains("Y confirms"));
        key(&ctx, 'y'); assert(contains("Y confirms"));
        key(&ctx, 'n'); assert(!contains("Y confirms"));
        key(&ctx, '\r'); key(&ctx, 'Y');
        assert(contains("negotiating") && contains("unconfirmed"));
        tick(&ctx, 100); assert(contains("negotiating"));
        tick(&ctx, 3100); assert(contains(outcomes[i]));
        if (i == 0) assert(contains("9000 mV / 1100 mA"));
        key(&ctx, 'q'); assert(ctx.exit_requested && ctx.exit_code == 0);
        end(&ctx);
    }
    ctx = (solar_os_context_t){.argc = 2};
    strcpy(ctx.argv[1], "--bad"); begin(&ctx);
    assert(ctx.exit_requested && ctx.exit_code == 2); end(&ctx);
    ctx = (solar_os_context_t){.argc = 2}; strcpy(ctx.argv[1], "--demo");
    rows = 5; cols = 20; begin(&ctx); assert(contains("Needs 16 rows"));
    key(&ctx, 'q'); end(&ctx);
    puts("power app lifecycle/scenarios passed");
}
