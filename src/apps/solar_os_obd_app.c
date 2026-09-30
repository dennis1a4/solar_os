#include "solar_os_obd_app.h"
#include "solar_os_keys.h"
#include "solar_os_obd_demo.h"
#include "solar_os_tui.h"
#include "solar_os_tui_widgets.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    solar_os_tui_t tui;
    solar_can_t bus;
    solar_obd_t obd;
    solar_obd_demo_t demo;
    unsigned ecu, category, top, saves;
    uint32_t now, next_render;
    bool started, confirming;
    char scenario[16], report_path[SOLAR_OS_APP_ARG_LEN], notice[160];
} obd_app_t;
static void *obd_state;
#define app (*(obd_app_t *)obd_state)
static const char *const categories[] = {"Stored", "Pending", "Permanent"};
static const char *const scenarios[] = {"normal", "timeout", "sequence", "reject"};
static void line(size_t row, const char *text, uint8_t attr) {
    if (row >= solar_os_tui_rows(&app.tui))
        return;
    char bounded[160];
    size_t width = solar_os_tui_cols(&app.tui);
    if (width >= sizeof(bounded))
        width = sizeof(bounded) - 1;
    size_t n = strlen(text);
    if (n > width)
        n = width;
    memcpy(bounded, text, n);
    bounded[n] = 0;
    solar_os_tui_addstr(&app.tui, row, 0, bounded, attr);
}
static unsigned bits(unsigned n) {
    unsigned c = 0;
    for (; n; n >>= 1)
        c += n & 1;
    return c;
}
static void render(void) {
    char text[160];
    size_t rows = solar_os_tui_rows(&app.tui);
    solar_os_tui_clear(&app.tui);
    line(0, "OBD-II  DEMO / SIMULATED ECUs - no vehicle connection", SOLAR_OS_TUI_ATTR_BOLD);
    if (rows < 12) {
        line(1, "Resize terminal to at least 12 rows. Q exits.", 0);
        solar_os_tui_refresh(&app.tui);
        return;
    }
    snprintf(text, sizeof(text), "Scenario: %s | %s | Tab: select ECU | 1/2/3: code category",
             app.scenario,
             app.obd.active ? (app.obd.clearing ? "clearing" : "scanning") : "scan finished");
    line(1, text, 0);
    const solar_obd_ecu_t *e = &app.obd.ecus[app.ecu];
    snprintf(text, sizeof(text), "ECU %03X  %s  %s", 0x7e8 + app.ecu,
             e->seen ? "responding" : "no response", categories[app.category]);
    line(3, text, SOLAR_OS_TUI_ATTR_BOLD);
    if (e->valid & 1) {
        unsigned supported = (e->readiness[1] & 7), incomplete = (e->readiness[1] >> 4) & supported;
        snprintf(text, sizeof(text),
                 "MIL: %s | stored count: %u | supported monitors: %u, incomplete: %u",
                 e->readiness[0] & 128 ? "ON" : "off", e->readiness[0] & 127,
                 bits(supported) + bits(e->readiness[2]),
                 bits(incomplete) + bits(e->readiness[2] & e->readiness[3]));
    } else
        snprintf(text, sizeof(text), "MIL/readiness unavailable (service 01 NRC=%02X)",
                 e->negative[0]);
    line(4, text, 0);
    snprintf(text, sizeof(text), "Readiness bytes: %02X %02X %02X %02X | clear: %s%s",
             e->readiness[0], e->readiness[1], e->readiness[2], e->readiness[3],
             e->clear_result == 1   ? "acknowledged; see rescan"
             : e->clear_result == 2 ? "no response"
             : e->clear_result == 3 ? "rejected"
                                    : "not requested",
             e->clear_result == 3 ? " (NRC in report)" : "");
    line(5, text, 0);
    if (!(e->valid & (2U << app.category))) {
        snprintf(text, sizeof(text),
                 "%s codes unavailable (NRC=%02X); this does not mean no codes.",
                 categories[app.category], e->negative[app.category + 1]);
        line(7, text, 0);
    } else if (!e->count[app.category])
        line(7, "No codes reported in this category.", 0);
    else {
        size_t available = rows - 12;
        if (!available)
            available = 1;
        for (size_t j = 0; j < available && j + app.top < e->count[app.category]; ++j) {
            unsigned index = j + app.top;
            char code[6];
            solar_obd_dtc(e->codes[app.category][index], code);
            snprintf(text, sizeof(text), "%2u  %s", index + 1, code);
            line(7 + j, text, 0);
        }
    }
    snprintf(text, sizeof(text), "RX %lu TX %lu | errors %lu timeouts %lu | dropped %lu",
             (unsigned long)app.obd.rx_frames, (unsigned long)app.obd.tx_frames,
             (unsigned long)app.obd.errors, (unsigned long)app.obd.timeouts,
             (unsigned long)(app.bus.rx.dropped + app.bus.tx.dropped));
    line(rows - 4, text, 0);
    line(rows - 3, app.notice, 0);
    if (app.confirming) {
        line(rows - 3, "Resets codes and readiness; permanent codes stay.", 0);
        snprintf(text, sizeof(text), "Clear ECU %03X? Y confirms; N cancels.", 0x7e8 + app.ecu);
        line(rows - 2, text, SOLAR_OS_TUI_ATTR_BOLD);
    } else
        line(rows - 2,
             "R rescan | Tab ECU | 1/2/3 category | arrows scroll | C clear | S save | Q quit",
             SOLAR_OS_TUI_ATTR_BOLD);
    solar_os_tui_refresh(&app.tui);
}
static void save(void) {
    char path[192];
    if (app.report_path[0])
        snprintf(path, sizeof(path), "%s", app.report_path);
    else
        snprintf(path, sizeof(path), "/sd/obd-demo-%lu-%u.txt", (unsigned long)app.now,
                 ++app.saves);
    FILE *f = fopen(path, "wx");
    if (!f) {
        strcpy(app.notice, "Cannot create report. Choose a new writable --report path.");
        return;
    }
    bool ok = solar_obd_report(&app.obd, f, app.scenario);
    if (fclose(f) != 0)
        ok = false;
    snprintf(app.notice, sizeof(app.notice), "%s: %.110s",
             ok ? "Saved DEMO report" : "Report write failed (partial file may remain)", path);
}
static esp_err_t start(solar_os_context_t *ctx) {
    bool demo = false;
    unsigned mode = 0;
    for (int i = 1; i < ctx->argc; ++i) {
        if (!strcmp(ctx->argv[i], "--demo"))
            demo = true;
        else if (!strcmp(ctx->argv[i], "--scenario") && i + 1 < ctx->argc) {
            const char *name = ctx->argv[++i];
            for (mode = 0; mode < 4 && strcmp(name, scenarios[mode]); ++mode) {
            }
            if (mode == 4)
                goto usage;
        } else if (!strcmp(ctx->argv[i], "--report") && i + 1 < ctx->argc) {
            snprintf(app.report_path, sizeof(app.report_path), "%s", ctx->argv[++i]);
            if (app.report_path[0] != '/')
                goto usage;
        } else
            goto usage;
    }
    if (!demo) {
        solar_os_context_finish(
            ctx, 2, "obd: hardware driver not installed. Use obd --demo (simulated ECUs).");
        return ESP_OK;
    }
    strcpy(app.scenario, scenarios[mode]);
    solar_can_init(&app.bus);
    app.bus.enabled[0] = true;
    app.bus.listen_only[0] = false;
    solar_obd_init(&app.obd, &app.bus, 0);
    solar_obd_demo_init(&app.demo, &app.bus, mode);
    strcpy(app.notice,
           "Simulation only. Clear acts on the selected simulated ECU; restart resets the demo.");
    esp_err_t err = solar_os_tui_screen_begin(&app.tui, ctx);
    if (err != ESP_OK)
        return err;
    render();
    return ESP_OK;
usage:
    solar_os_context_finish(
        ctx, 2,
        "usage: obd --demo [--scenario normal|timeout|sequence|reject] [--report /new/path.txt]");
    return ESP_OK;
}
static void stop(solar_os_context_t *ctx) {
    (void)ctx;
    solar_os_tui_end(&app.tui);
}
static bool event(solar_os_context_t *ctx, const solar_os_event_t *ev) {
    if (ev->type == SOLAR_OS_EVENT_TICK) {
        app.now = ev->data.tick_ms;
        if (!app.started) {
            app.started = true;
            app.next_render = app.now;
            solar_obd_scan(&app.obd, app.now);
        }
        solar_obd_demo_poll(&app.demo, app.now);
        solar_obd_poll(&app.obd, app.now);
        if ((int32_t)(app.now - app.next_render) >= 0) {
            app.next_render = app.now + 250;
            render();
        }
        return true;
    }
    if (ev->type != SOLAR_OS_EVENT_CHAR)
        return false;
    unsigned char key = ev->data.ch;
    if (key == 'q' || key == 'Q' || key == SOLAR_OS_KEY_APP_EXIT) {
        solar_os_context_finish(ctx, 0, NULL);
        return true;
    }
    if (app.confirming) {
        if (key == 'Y') {
            if (solar_obd_clear(&app.obd, app.ecu, app.now))
                strcpy(app.notice,
                       "Clear requested. Waiting for ECU acknowledgement, then rescanning.");
            else
                strcpy(app.notice, "Clear not started: wait for scan and select a responding ECU.");
            app.confirming = false;
        } else if (key == 'n' || key == 'N' || key == SOLAR_OS_KEY_ESCAPE)
            app.confirming = false;
        render();
        return true;
    }
    switch (key) {
    case SOLAR_OS_KEY_ESCAPE:
        solar_os_context_finish(ctx, 0, NULL);
        return true;
    case 'r':
    case 'R':
        if (!solar_obd_scan(&app.obd, app.now))
            strcpy(app.notice, "Scan already active.");
        else
            strcpy(app.notice, "Reading readiness and all three code categories.");
        break;
    case '\t':
        app.ecu = (app.ecu + 1) % SOLAR_OBD_ECUS;
        app.top = 0;
        break;
    case '1':
    case '2':
    case '3':
        app.category = key - '1';
        app.top = 0;
        break;
    case SOLAR_OS_KEY_DOWN:
        if (app.top + 1 < app.obd.ecus[app.ecu].count[app.category])
            ++app.top;
        break;
    case SOLAR_OS_KEY_UP:
        if (app.top)
            --app.top;
        break;
    case 'c':
    case 'C':
        if (app.obd.active || !app.obd.ecus[app.ecu].seen)
            strcpy(app.notice, "Wait for scan completion and select a responding ECU.");
        else
            app.confirming = true;
        break;
    case 's':
    case 'S':
        save();
        break;
    default:
        return true;
    }
    render();
    return true;
}
const solar_os_app_t solar_os_obd_app = {
    .name = "obd",
    .summary = "OBD-II diagnostics (simulated CAN backend)",
    .app_class = SOLAR_OS_APP_CLASS_TUI,
    .start = start,
    .stop = stop,
    .event = event,
    .tick_interval_ms = 10,
    .state_slot = &obd_state,
    .state_size = sizeof(obd_app_t),
    .state_storage = SOLAR_OS_APP_STATE_EXTERNAL_REQUIRED,
};
