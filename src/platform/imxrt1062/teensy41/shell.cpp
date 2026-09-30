#if SK_UPSTREAM_SHELL && !SK_LCD_CONSOLE
#include <arduino_freertos.h>
#include "platform.h"
extern "C" {
#include "solar_os.h"
#include "solar_os_shell.h"
#include "solar_os_shell_io.h"
#include "solar_os_app_registry.h"
#include "solar_os_port.h"
#include "solar_os_keys.h"
#include "solar_os_vt100.h"
#include "solar_os_sessions.h"
#include "solar_os_memory.h"
#include "solar_os_tui.h"
#if SK_SETTINGS
#include "nvs.h"
#endif
}

static solar_os_context_t shell_context;
static solar_os_shell_session_t *session;
static const solar_os_app_t *foreground;
static solar_os_tui_t *active_tui;
extern "C" void solar_os_sessions_attach_tui(solar_os_shell_io_t *io, solar_os_tui_t *tui) {
    configASSERT(session && io == solar_os_shell_session_io(session));
    active_tui = tui;
}
extern "C" void solar_os_sessions_detach_tui(solar_os_shell_io_t *io, const solar_os_tui_t *tui) {
    if (session && io == solar_os_shell_session_io(session) && active_tui == tui) active_tui = nullptr;
}
extern "C" size_t solar_os_sessions_shell_count() { return session ? 1 : 0; }
static const char *owner = "usb-shell";
static esp_err_t usb_write(void *, const uint8_t *data, size_t length, size_t *written) {
    *written = Serial ? Serial.write(data, length) : 0;
    return *written == length ? ESP_OK : ESP_ERR_TIMEOUT;
}
static esp_err_t usb_read(void *, uint8_t *data, size_t length, uint32_t timeout, size_t *received) {
    const uint32_t started = millis();
    *received = 0;
    do {
        while (*received < length && Serial.available()) data[(*received)++] = Serial.read();
        if (*received || millis() - started >= timeout) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(1));
    } while (true);
}
// The interpreter runs synchronously in this console task. Check for
// interrupt bytes. Python has no stdin in this profile, so discard typeahead
// while busy (including the LF following a CRLF submission).
extern "C" bool sk_python_poll_cancel() {
    if (!Serial) return true;
    bool interrupted = false;
    while (Serial.available()) {
        const int ch = Serial.read();
        interrupted |= ch == 3 || ch == 29;
    }
    return interrupted;
}
extern "C" uint32_t sk_python_random_seed() { return micros() ^ ARM_DWT_CYCCNT; }
#if SK_FILES
#include "shell_children.h"
#else
static solar_os_context_t *current_context() { return &shell_context; }
static void finish_app() {
    const bool had_screen = active_tui != nullptr;
    solar_os_app_stop(foreground, &shell_context);
    if (had_screen) solar_os_shell_io_clear(solar_os_shell_session_io(session));
    solar_os_app_registry_release(foreground, owner);
    foreground = nullptr;
    solar_os_shell_session_set_foreground_app(session, nullptr);
    solar_os_context_set_app_class(&shell_context, SOLAR_OS_APP_CLASS_TUI);
    solar_os_shell_session_prompt(&shell_context, session);
}
static void service_requests() {
    if (solar_os_context_take_exit_request(&shell_context)) {
        if (foreground) finish_app();
        else {
            // The single physical console remains available after `exit`.
            solar_os_shell_io_writeln(solar_os_shell_session_io(session), "Console session restarted.");
            solar_os_shell_session_start(&shell_context, session,
                solar_os_shell_session_io(session), false, false);
        }
    }
    const solar_os_app_t *app = solar_os_context_take_launch_request(&shell_context);
    if (!app) return;
    if (foreground) {
        solar_os_context_finish(&shell_context, 1, "Nested application launch is not supported.");
        return;
    }
    esp_err_t err = solar_os_app_registry_claim(app, owner, nullptr, 0);
    if (err != ESP_OK) {
        solar_os_context_finish(&shell_context, 1, esp_err_to_name(err));
        solar_os_context_take_exit_request(&shell_context);
        solar_os_shell_session_prompt(&shell_context, session);
        return;
    }
    foreground = app;
    solar_os_shell_session_set_foreground_app(session, app);
    err = solar_os_app_start(app, &shell_context);
    const bool finished = solar_os_context_take_exit_request(&shell_context);
    if (err != ESP_OK || finished) finish_app();
}
#endif
static bool emit_key(char ch, void *) {
    solar_os_event_t event{};
    event.type = SOLAR_OS_EVENT_CHAR;
    event.data.ch = ch == 3 && (!foreground || !strcmp(foreground->name, "calc"))
        ? SOLAR_OS_KEY_ESCAPE : ch;
    if (foreground) {
        if (foreground->event) foreground->event(current_context(), &event);
        else if (static_cast<uint8_t>(ch) == SOLAR_OS_KEY_APP_EXIT) solar_os_context_finish(current_context(), 0, nullptr);
    } else solar_os_shell_session_event(&shell_context, session, &event);
    service_requests();
    return true;
}
void sk_upstream_shell_run() {
    const solar_os_port_driver_t usb = {"usb", "Teensy USB CDC",
        SOLAR_OS_PORT_CAP_READ | SOLAR_OS_PORT_CAP_WRITE, usb_read, usb_write, nullptr, nullptr, nullptr};
    configASSERT(solar_os_port_register(&usb) == ESP_OK);
    solar_os_port_handle_t port = SOLAR_OS_PORT_HANDLE_INIT;
    configASSERT(solar_os_port_claim("usb", owner, &port) == ESP_OK);
    session = solar_os_shell_session_create();
    configASSERT(session);
    solar_os_context_init(&shell_context, nullptr, nullptr);
    auto *io = solar_os_shell_session_io(session);
    solar_os_shell_io_init_port(io, &port, 80, 24);
#if SK_SETTINGS
    nvs_handle_t settings;
    if (nvs_open("usb_terminal",NVS_READONLY,&settings)==ESP_OK) {
        uint16_t cols=80,rows=24;
        if (nvs_get_u16(settings,"cols",&cols)==ESP_OK &&
            nvs_get_u16(settings,"rows",&rows)==ESP_OK &&
            cols>=20 && cols<=300 && rows>=8 && rows<=120)
            solar_os_shell_io_set_dimensions(io,cols,rows);
        nvs_close(settings);
    }
#endif
    solar_os_shell_io_set_terminal_profile(io, SOLAR_OS_SHELL_TERMINAL_PROFILE_VT100);
    solar_os_vt100_input_t input;
    solar_os_vt100_input_init(&input);
    bool connected = false, was_cr = false;
    uint32_t last_byte = 0, last_tick = 0;
    while (true) {
        if (foreground && foreground->event && millis()-last_tick >= 10) {
            last_tick = millis();
            solar_os_event_t event{}; event.type = SOLAR_OS_EVENT_TICK;
            foreground->event(current_context(), &event);
            service_requests();
        }
        if (Serial && !connected) {
            connected = true;
            solar_os_vt100_input_reset(&input);
            was_cr = false;
            while (foreground) {
                solar_os_context_finish(current_context(), 0, nullptr);
                service_requests();
            }
            solar_os_shell_session_start(&shell_context, session, io, false, SK_SETTINGS);
            service_requests();
        } else if (!Serial) connected = false;
        if (connected && !foreground && millis()-last_tick >= 25) {
            last_tick = millis();
            solar_os_event_t tick{};
            tick.type = SOLAR_OS_EVENT_TICK; tick.data.tick_ms = pdTICKS_TO_MS(xTaskGetTickCount());
            solar_os_shell_session_event(&shell_context, session, &tick);
            service_requests();
        }
        if (connected && Serial.available()) {
            const uint8_t ch = Serial.read();
            last_byte = millis();
            if (ch == '\n' && was_cr) { was_cr = false; continue; }
            was_cr = ch == '\r';
            solar_os_vt100_input_feed_byte(&input, ch, emit_key, nullptr);
        } else {
            if (solar_os_vt100_input_pending(&input) && millis() - last_byte >= 40)
                solar_os_vt100_input_flush(&input, emit_key, nullptr);
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
}
#endif
