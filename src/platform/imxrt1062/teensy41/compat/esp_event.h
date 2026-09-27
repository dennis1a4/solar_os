#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef const char *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
#define ESP_EVENT_DECLARE_BASE(name) extern esp_event_base_t const name
#define ESP_EVENT_DEFINE_BASE(name) esp_event_base_t const name = #name
#define ESP_EVENT_ANY_ID (-1)
#define ESP_EVENT_ANY_BASE NULL
esp_err_t esp_event_post(esp_event_base_t base, int32_t id, const void *data, size_t size, TickType_t wait);
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t handler, void *arg);
esp_err_t esp_event_handler_unregister(esp_event_base_t base, int32_t id, esp_event_handler_t handler);
#ifdef __cplusplus
}
#endif
