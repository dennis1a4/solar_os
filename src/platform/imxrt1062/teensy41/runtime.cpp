#include <arduino_freertos.h>
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
    default: return "UNKNOWN";
    }
}
extern "C" void esp_restart() {
    SCB_AIRCR = 0x05FA0004;
    asm volatile("dsb" ::: "memory");
    while (true) {}
}
extern "C" esp_err_t solar_os_log_write(solar_os_log_level_t level,
                                         const char *tag, const char *fmt, ...) {
    char message[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    sk_console_printf("[%u] %s: %s\r\n", unsigned(level), tag ? tag : "", message);
    return ESP_OK;
}
// This bring-up uses context output callbacks. Full shell I/O/session service
// integration remains separate; do not substitute successful no-op services.
extern "C" void solar_os_shell_io_capture_output(solar_os_shell_io_t *io,
                                                 solar_os_context_t *ctx) {
    if (!io) return;
    io->output_mirror_fn = solar_os_context_output_handler(ctx, &io->output_mirror_user);
}
