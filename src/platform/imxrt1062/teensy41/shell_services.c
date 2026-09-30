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
#include "solar_os_shell.h"
#include "solar_os_storage.h"

#if !SK_SETTINGS
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
#endif
#if !SK_PLAYGROUND
bool solar_os_board_has(solar_os_board_capability_t cap) {
    solar_os_memory_status_t status;
    solar_os_memory_get_status(&status);
    const uint64_t supported = SOLAR_OS_BOARD_CAP_CDC | SOLAR_OS_BOARD_CAP_SD |
        (status.external.total ? SOLAR_OS_BOARD_CAP_PSRAM : 0);
    return (supported & cap) == cap;
}
#endif
#if !SK_SSH
bool solar_os_task_admit(const char *name, uint32_t size,
                        solar_os_task_role_t role, bool external) {
    (void)name; (void)role; (void)external;
    // Only apps without worker tasks are supported by this initial runtime.
    return size == 0;
}
#endif
void solar_os_shell_cmd_apps(solar_os_context_t *ctx, int argc, char **argv) {
    (void)argc; (void)argv;
    solar_os_shell_io_t *io = solar_os_context_shell_io(ctx);
    for (size_t i = 0; i < solar_os_app_registry_count(); ++i) {
        const solar_os_app_registry_entry_t *app = solar_os_app_registry_get(i);
        solar_os_shell_io_write_bold(io, app->name);
        solar_os_shell_io_printf(io, " - %s\n", app->summary);
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
#if SK_CLOCK
#include "solar_os_time.h"
#endif
void solar_os_shell_cmd_setterm(solar_os_context_t *ctx, int argc, char **argv) {
    solar_os_shell_io_t *io = solar_os_context_shell_io(ctx);
#if SK_CLOCK
    if ((argc==2 || argc==3) && !strcmp(argv[1],"timezone")) {
        if (argc==3) {
            esp_err_t err=solar_os_time_set_timezone(argv[2]);
            if (err!=ESP_OK) {
                solar_os_shell_io_printf(io,"timezone not saved: %s\n",esp_err_to_name(err));
                return;
            }
        }
        char name[SOLAR_OS_TIMEZONE_NAME_MAX], posix[SOLAR_OS_TIMEZONE_POSIX_MAX];
        solar_os_time_get_timezone(name,sizeof(name),posix,sizeof(posix));
        solar_os_shell_io_printf(io,"timezone %s (%s); RTC stays UTC\n",name,posix);
        return;
    }
#endif
#if SK_SETTINGS
    if (argc == 1) {
        char path[SOLAR_OS_STORAGE_PATH_MAX];
        solar_os_shell_startup_path(path,sizeof(path));
        solar_os_shell_io_printf(io,"size %u %u; startup %s (%s)\n",
            (unsigned)solar_os_shell_io_cols(io),(unsigned)solar_os_shell_io_rows(io),
            solar_os_shell_startup_source_name(solar_os_shell_startup_source()),path);
        return;
    }
    if (argc == 3 && !strcmp(argv[1],"startup")) {
        solar_os_shell_startup_source_t source;
        esp_err_t err=solar_os_shell_parse_startup_source(argv[2],&source) ?
            solar_os_shell_set_startup_source(source) : ESP_ERR_INVALID_ARG;
        solar_os_shell_io_printf(io,"startup: %s\n",err==ESP_OK ? "saved" : esp_err_to_name(err));
        return;
    }
#endif
    if (argc == 4 && !strcmp(argv[1], "size")) {
#if SK_LCD_CONSOLE
        extern bool sk_console_is_local(void);
        if (sk_console_is_local()) {
            solar_os_shell_io_writeln(io,"LCD geometry is fixed at 100x30.");
            return;
        }
#endif
        char *end_col, *end_row;
        long cols = strtol(argv[2], &end_col, 10), rows = strtol(argv[3], &end_row, 10);
        if (*argv[2] && *argv[3] && !*end_col && !*end_row &&
            cols >= 20 && cols <= 300 && rows >= 8 && rows <= 120) {
#if SK_SETTINGS
#if SK_TELNETD
            extern bool sk_console_is_remote(void);
            if(sk_console_is_remote()) {
                solar_os_shell_io_writeln(io,"Telnet geometry comes from the client window (NAWS)."); return;
            }
#endif
            nvs_handle_t h;
            esp_err_t err=nvs_open("usb_terminal",NVS_READWRITE,&h);
            if (err==ESP_OK) {
                err=nvs_set_u16(h,"cols",cols);
                if (err==ESP_OK) err=nvs_set_u16(h,"rows",rows);
                if (err==ESP_OK) err=nvs_commit(h);
                nvs_close(h);
            }
            if (err!=ESP_OK) {
                solar_os_shell_io_printf(io,"size not saved: %s\n",esp_err_to_name(err));
                return;
            }
#endif
            solar_os_shell_io_set_dimensions(io, cols, rows);
            solar_os_shell_io_clear(io);
            return;
        }
    }
#if SK_CLOCK
    solar_os_shell_io_writeln(io,"       setterm timezone [UTC|Manitoba|UTC+/-offset|Europe/Berlin|POSIX-TZ]");
#endif
    solar_os_shell_io_writeln(io, "usage: setterm size <cols 20..300> <rows 8..120> (default 80 24)");
#if SK_SETTINGS
    solar_os_shell_io_writeln(io,"       setterm startup <auto|flash|sd>; setterm (show settings)");
#endif
}
#if SK_SETTINGS
void solar_os_shell_cmd_identity(solar_os_context_t *ctx,int argc,char **argv) {
    solar_os_shell_io_t *io=solar_os_context_shell_io(ctx);
    if (argc==1 || (argc==2 && !strcmp(argv[1],"status"))) {
        char identity[80]; solar_os_identity_format(identity,sizeof(identity));
        solar_os_shell_io_writeln(io,identity); return;
    }
    if (argc==3 && (!strcmp(argv[1],"user") || !strcmp(argv[1],"hostname"))) {
        esp_err_t err=!strcmp(argv[1],"user") ? solar_os_identity_set_user(argv[2]) : solar_os_identity_set_hostname(argv[2]);
        solar_os_shell_io_printf(io,"identity: %s\n",err==ESP_OK ? "saved" : esp_err_to_name(err));
        return;
    }
    solar_os_shell_io_writeln(io,"usage: identity [status|user <name>|hostname <name>] (1-31 letters, digits, ., -, _)");
}
#endif
#endif
