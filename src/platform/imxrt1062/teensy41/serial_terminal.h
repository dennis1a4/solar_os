#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void sk_serial_init(void);
void sk_serial_settings(const char *bus, char *out, size_t capacity);
esp_err_t sk_serial_attach(const char *bus, uint32_t baud);
void sk_serial_detach(const char *bus);
esp_err_t sk_serial_read(const char *bus, uint8_t *data, size_t capacity, size_t *received);
esp_err_t sk_serial_write(const char *bus, const uint8_t *data, size_t length, size_t *written);
#ifdef __cplusplus
}
#endif
