#pragma once
#ifndef SK_SETTINGS
#define SK_SETTINGS 0
#endif
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

void sk_console_begin();
void sk_console_write(const char *text, size_t length);
void sk_console_print(const char *text);
int sk_console_read();
void sk_console_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
esp_err_t sk_buses_begin();
bool sk_spi_lock(unsigned bus);
void sk_spi_unlock(unsigned bus);
bool sk_i2c_lock(unsigned bus);
void sk_i2c_unlock(unsigned bus);
esp_err_t sk_i2c_transfer(unsigned bus, uint8_t address,
                          const uint8_t *tx, size_t tx_length,
                          uint8_t *rx, size_t rx_length);
esp_err_t sk_slot_claim(unsigned slot, const char *owner);
esp_err_t sk_slot_release(unsigned slot, const char *owner);
esp_err_t sk_slot_spi(unsigned slot, const char *owner, uint32_t hz, uint8_t mode,
                      const uint8_t *tx, uint8_t *rx, size_t length);
esp_err_t sk_slot_uart_begin(unsigned slot, const char *owner, uint32_t baud);
int sk_slot_uart_read(unsigned slot, const char *owner);
esp_err_t sk_slot_uart_write(unsigned slot, const char *owner, const uint8_t *data, size_t length);
void sk_slots_print();
esp_err_t sk_sd_mount();
void sk_sd_print_status();
void sk_sd_list(const char *path);
void sk_sd_cat(const char *path);
void sk_memory_begin();
void sk_memory_print();
void sk_displays_begin();
void sk_small_monitor_status(char *out, size_t size, int enabled = -1);
void sk_display_write(const char *text, size_t length);
void sk_usb_begin();
void sk_usb_poll();
int sk_usb_read();
void sk_audio_begin();
void sk_audio_tone(bool on);

bool sk_sd_is_mounted();

#if SK_HW_RESOURCES
esp_err_t sk_resources_begin();
esp_err_t sk_uart_claim_format(unsigned slot,const char *owner,uint32_t baud,uint16_t format);
esp_err_t sk_uart_claim(unsigned slot,const char *owner,uint32_t baud);
esp_err_t sk_uart_release(unsigned slot,const char *owner);
void sk_hardware_release(const char *owner);
const char *sk_slot_owner(unsigned slot);
#endif
