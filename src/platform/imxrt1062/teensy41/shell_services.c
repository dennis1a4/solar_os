#if SK_UPSTREAM_SHELL
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvs.h"
#include "solar_os_shell_commands.h"
#include "solar_os_shell_io.h"
#include "solar_os_app_registry.h"
#include "solar_os_memory.h"
#include "solar_os_identity.h"
#include "solar_os_board_caps.h"
#include "solar_os_task.h"

// Persistent settings are not available in this SD profile.
// Return an explicit failure; never report a successful discarded write.
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *out) {
    (void)name; (void)mode; (void)out; return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *v) {
    (void)h; (void)k; (void)v; return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v) {
    (void)h; (void)k; (void)v; return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_ERR_NOT_SUPPORTED; }
void nvs_close(nvs_handle_t h) { (void)h; }
void solar_os_identity_format(char *buffer, size_t len) { snprintf(buffer, len, "user@teensy41"); }
bool solar_os_board_has(solar_os_board_capability_t cap) {
    solar_os_memory_status_t status;
    solar_os_memory_get_status(&status);
    const uint64_t supported = SOLAR_OS_BOARD_CAP_CDC | SOLAR_OS_BOARD_CAP_SD |
        (status.external.total ? SOLAR_OS_BOARD_CAP_PSRAM : 0);
    return (supported & cap) == cap;
}
bool solar_os_task_admit(const char *name, uint32_t size,
                        solar_os_task_role_t role, bool external) {
    (void)name; (void)role; (void)external;
    // Only apps without worker tasks are supported by this initial runtime.
    return size == 0;
}
void solar_os_shell_cmd_apps(solar_os_context_t *ctx, int argc, char **argv) {
    (void)argc; (void)argv;
    for (size_t i = 0; i < solar_os_app_registry_count(); ++i) {
        const solar_os_app_registry_entry_t *app = solar_os_app_registry_get(i);
        solar_os_shell_io_printf(solar_os_context_shell_io(ctx), "%s - %s\n", app->name, app->summary);
    }
}
void solar_os_shell_cmd_mem(solar_os_context_t *ctx, int argc, char **argv) {
    (void)argc; (void)argv;
    solar_os_memory_status_t status;
    solar_os_memory_get_status(&status);
    solar_os_shell_io_printf(solar_os_context_shell_io(ctx),
        "Internal heap: %u free / %u bytes; PSRAM: %u free / %u bytes\n",
        (unsigned)status.internal.free, (unsigned)status.internal.total,
        (unsigned)status.external.free, (unsigned)status.external.total);
}
void solar_os_shell_cmd_uptime(solar_os_context_t *ctx, int argc, char **argv) {
    (void)argc; (void)argv;
    solar_os_shell_io_printf(solar_os_context_shell_io(ctx), "Uptime=%lu ms stack-free=%lu words\n",
        (unsigned long)pdTICKS_TO_MS(xTaskGetTickCount()),
        (unsigned long)uxTaskGetStackHighWaterMark(NULL));
}
void solar_os_shell_cmd_clear(solar_os_context_t *ctx, int argc, char **argv) {
    (void)argc; (void)argv; solar_os_shell_io_clear(solar_os_context_shell_io(ctx));
}
// Geometry is explicit because a serial connection does not carry window size.
void solar_os_shell_cmd_setterm(solar_os_context_t *ctx, int argc, char **argv) {
    solar_os_shell_io_t *io = solar_os_context_shell_io(ctx);
    if (argc == 4 && !strcmp(argv[1], "size")) {
        char *end_col, *end_row;
        long cols = strtol(argv[2], &end_col, 10), rows = strtol(argv[3], &end_row, 10);
        if (*argv[2] && *argv[3] && !*end_col && !*end_row &&
            cols >= 20 && cols <= 300 && rows >= 8 && rows <= 120) {
            solar_os_shell_io_set_dimensions(io, cols, rows);
            solar_os_shell_io_clear(io);
            return;
        }
    }
    solar_os_shell_io_writeln(io, "usage: setterm size <cols 20..300> <rows 8..120> (default 80 24)");
}
#endif
