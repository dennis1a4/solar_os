#include "solar_os_power_app.h"
#include "solar_os_pd_power.h"
#include "solar_os_keys.h"
#include "solar_os_tui.h"
#include "solar_os_tui_widgets.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    solar_os_tui_t tui;
    solar_pd_power_t power;
    solar_pd_power_demo_t demo;
    unsigned selected;
    uint16_t ma;
    uint32_t now;
    bool confirming;
} power_app_t;
static void *power_state;
#define app (*(power_app_t *)power_state)
static const char *const modes[] = {"normal", "reject", "timeout", "disconnect", "io-error", "mismatch"};
static void line(unsigned row, const char *text, uint8_t attr) {
    if (row >= solar_os_tui_rows(&app.tui)) return;
    char out[128];
    size_t n = solar_os_tui_cols(&app.tui);
    if (n >= sizeof(out)) n = sizeof(out) - 1;
    snprintf(out, sizeof(out), "%.*s", (int)n, text);
    solar_os_tui_addstr(&app.tui, row, 0, out, attr);
}
static void render(void) {
    char text[128];
    solar_os_tui_clear(&app.tui);
    line(0, "POWER - SIMULATION ONLY", SOLAR_OS_TUI_ATTR_BOLD);
    if (solar_os_tui_rows(&app.tui) < 16) {
        line(1, "Needs 16 rows. Q exits.", 0);
        solar_os_tui_refresh(&app.tui); return;
    }
    snprintf(text, sizeof(text), "Scenario: %s | %s", modes[app.demo.mode], solar_pd_power_state_name(app.power.state));
    line(1, text, 0);
    line(2, "No I2C access or real voltage change. Restart resets demo.", 0);
    line(3, "Source fixed profiles (voltage / available current):", SOLAR_OS_TUI_ATTR_BOLD);
    for (unsigned i = 0; i < app.power.source.count; ++i) {
        solar_pd_power_profile_t p = app.power.source.profiles[i];
        snprintf(text, sizeof(text), "%c %u: %u V / %u mA", i == app.selected ? '>' : ' ', i + 1, p.mv / 1000, p.ma);
        line(4 + i, text, i == app.selected ? SOLAR_OS_TUI_ATTR_BOLD : 0);
    }
    if (app.power.source.contract_valid && app.power.state != SOLAR_PD_POWER_WAITING)
        snprintf(text, sizeof(text), "Reported contract: %u mV / %u mA", app.power.source.contract.mv, app.power.source.contract.ma);
    else snprintf(text, sizeof(text), "Contract unconfirmed / unavailable");
    line(9, text, 0);
    snprintf(text, sizeof(text), "Request current: %u mA | demo limits: 20 V / 3000 mA", app.ma);
    line(10, text, 0);
    line(11, "Current is a contract value, not measured consumption.", 0);
    line(12, "1-4 select | +/- current (100mA) | Enter request | Q exit", 0);
    line(13, app.confirming ? "Apply selected DEMO request? Y confirms; N cancels." : "Settings are temporary. Hardware backend is not enabled.", SOLAR_OS_TUI_ATTR_BOLD);
    solar_os_tui_refresh(&app.tui);
}
static esp_err_t start(solar_os_context_t *ctx) {
    bool demo = false;
    unsigned mode = 0;
    for (int i = 1; i < ctx->argc; ++i) {
        if (!strcmp(ctx->argv[i], "--demo") && !demo) demo = true;
        else if (!strcmp(ctx->argv[i], "--scenario") && i + 1 < ctx->argc) {
            const char *name = ctx->argv[++i];
            for (mode = 0; mode < 6 && strcmp(name, modes[mode]); ++mode) {}
            if (mode == 6) goto usage;
        } else goto usage;
    }
    if (!demo) {
        solar_os_context_finish(ctx, 2, "pdpower: hardware backend not configured. Use pdpower --demo. I2C wiring and board limits pending.");
        return ESP_OK;
    }
    app.ma = 1000;
    solar_pd_power_demo_init(&app.demo, (solar_pd_power_demo_mode_t)mode);
    solar_pd_power_init(&app.power, &solar_pd_power_demo_backend, &app.demo, 20000, 3000);
    solar_pd_power_poll(&app.power, 0);
    esp_err_t err = solar_os_tui_screen_begin(&app.tui, ctx);
    if (err != ESP_OK) return err;
    render(); return ESP_OK;
usage:
    solar_os_context_finish(ctx, 2, "usage: pdpower --demo [--scenario normal|reject|timeout|disconnect|io-error|mismatch]");
    return ESP_OK;
}
static void stop(solar_os_context_t *ctx) {
    (void)ctx;
    if (app.tui.screen_active) solar_os_tui_end(&app.tui);
}
static bool event(solar_os_context_t *ctx, const solar_os_event_t *ev) {
    if (ev->type == SOLAR_OS_EVENT_TICK) {
        app.now = ev->data.tick_ms;
        solar_pd_power_state_t old = app.power.state;
        solar_pd_power_poll(&app.power, app.now);
        if (old != app.power.state) { app.confirming = false; render(); }
        return true;
    }
    if (ev->type != SOLAR_OS_EVENT_CHAR) return false;
    unsigned char key = ev->data.ch;
    if (key == 'q' || key == 'Q' || key == SOLAR_OS_KEY_APP_EXIT || key == SOLAR_OS_KEY_ESCAPE) {
        solar_os_context_finish(ctx, 0, NULL); return true;
    }
    if (app.confirming) {
        if (key == 'Y') {
            solar_pd_power_request(&app.power, app.selected, app.ma, app.now);
            app.confirming = false;
        } else if (key == 'n' || key == 'N') app.confirming = false;
    } else if (app.power.state != SOLAR_PD_POWER_WAITING) {
        if (key >= '1' && key <= '4') app.selected = key - '1';
        else if (key == '+' && app.ma < 3000) app.ma += 100;
        else if (key == '-' && app.ma > 100) app.ma -= 100;
        else if ((key == '\r' || key == '\n') && app.power.source.attached &&
                 app.selected < app.power.source.count) app.confirming = true;
    }
    render(); return true;
}
const solar_os_app_t solar_os_power_app = {
    .name = "pdpower", .summary = "USB-PD power profiles (simulation)",
    .app_class = SOLAR_OS_APP_CLASS_TUI,
    .start = start, .stop = stop, .event = event, .tick_interval_ms = 50,
    .state_slot = &power_state, .state_size = sizeof(power_app_t),
    .state_storage = SOLAR_OS_APP_STATE_EXTERNAL_REQUIRED,
};
