#include <arduino_freertos.h>
#include <semphr.h>
#include <stdarg.h>
#include "platform.h"

static SemaphoreHandle_t console_mutex;
void sk_console_begin() {
    Serial.begin(115200);
    Serial1.begin(115200);
    console_mutex = xSemaphoreCreateMutex();
    configASSERT(console_mutex);
}
void sk_console_write(const char *text, size_t length) {
    if (!text || !length) return;
    if (xSemaphoreTake(console_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    // Serial1 works without a USB host. Never wait for USB enumeration.
    Serial1.write(reinterpret_cast<const uint8_t *>(text), length);
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
    if (Serial1.available()) return Serial1.read();
    return sk_usb_read();
}
