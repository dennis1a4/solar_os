#include <arduino_freertos.h>
#include "small_display.h"
#include "platform.h"
extern "C" {
#include "solar_os.h"
#include "solar_os_log.h"
#include "solar_os_shell_io.h"
}

extern "C" const char *esp_err_to_name(esp_err_t error) {
    switch (error) {
    case ESP_OK: return "OK";
    case ESP_FAIL: return "FAIL";
    case ESP_ERR_NO_MEM: return "NO_MEM";
    case ESP_ERR_INVALID_ARG: return "INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "INVALID_STATE";
    case ESP_ERR_INVALID_SIZE: return "INVALID_SIZE";
    case ESP_ERR_NOT_FOUND: return "NOT_FOUND";
    case ESP_ERR_NOT_SUPPORTED: return "NOT_SUPPORTED";
    case ESP_ERR_TIMEOUT: return "TIMEOUT";
    case ESP_ERR_INVALID_CRC: return "INVALID_CRC";
    case ESP_ERR_NOT_ALLOWED: return "NOT_ALLOWED";
    default: return "UNKNOWN";
    }
}
#if SK_SETTINGS && SK_LCD_CONSOLE
extern "C" bool sk_console_history_flush();
#endif
extern "C" void esp_restart() {
#if SK_SETTINGS && SK_LCD_CONSOLE
    sk_console_history_flush();
#endif
    SCB_AIRCR = 0x05FA0004;
    asm volatile("dsb" ::: "memory");
    while (true) {}
}
extern "C" esp_err_t solar_os_log_write(solar_os_log_level_t level,
                                         const char *tag, const char *fmt, ...) {
#if SK_PLAYGROUND && !SK_CURL_DIAGNOSTICS
    // Curl sends progress/errors through its owning console's event queue.
    // Its worker must not also write raw URL diagnostics to the USB console.
    if (tag && !strcmp(tag,"solar_os_curl")) return ESP_OK;
#endif
#if SK_GRAPHICS && !SK_GFX_DIAGNOSTICS
    // These local graphics lifecycle diagnostics must not interrupt USB prompts.
    if (tag && (!strcmp(tag,"gfx") || !strcmp(tag,"solar_os_view"))) return ESP_OK;
#endif

    char message[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
#if SK_GAMEBOY
    if(tag && !strcmp(tag,"solar_os_gameboy")) {
        extern void sk_gameboy_log(const char *);
        sk_gameboy_log(message);
        return ESP_OK;
    }
#endif
    sk_console_printf("[%u] %s: %s\r\n", unsigned(level), tag ? tag : "", message);
    return ESP_OK;
}
// This bring-up uses context output callbacks. Full shell I/O/session service
// integration remains separate; do not substitute successful no-op services.
#if !SK_UPSTREAM_SHELL
extern "C" void solar_os_shell_io_capture_output(solar_os_shell_io_t *io,
                                                 solar_os_context_t *ctx) {
    if (!io) return;
    io->output_mirror_fn = solar_os_context_output_handler(ctx, &io->output_mirror_user);
}

#endif
