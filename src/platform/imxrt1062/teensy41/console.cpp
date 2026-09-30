#include <arduino_freertos.h>
#include <semphr.h>
#include <stdarg.h>
#include "platform.h"
#include "board.h"

static SemaphoreHandle_t console_mutex;
void sk_console_begin() {
    Serial.begin(115200);
#if SK_UART_CONSOLE
    Serial1.begin(115200);
#endif
    console_mutex = xSemaphoreCreateMutex();
    configASSERT(console_mutex);
}
void sk_console_write(const char *text, size_t length) {
    if (!text || !length) return;
    if (xSemaphoreTake(console_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
#if SK_UART_CONSOLE
    // Optional UART console; disabled when pin 0 is AmpEn.
    Serial1.write(reinterpret_cast<const uint8_t *>(text), length);
#endif
    if (Serial) Serial.write(reinterpret_cast<const uint8_t *>(text), length);
    xSemaphoreGive(console_mutex);
}
void sk_console_print(const char *text) { sk_console_write(text, strlen(text)); }
void sk_console_printf(const char *fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    sk_console_print(buffer);
}
int sk_console_read() {
    if (Serial.available()) return Serial.read();
#if SK_UART_CONSOLE
    if (Serial1.available()) return Serial1.read();
#endif
    return sk_usb_read();
}
