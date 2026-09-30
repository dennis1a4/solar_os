#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SOLAR_SCOPE_SAMPLES 1024U
/* Hardware backend: one-shot timer/DMA captures, gaps between captures. */
bool solar_scope_capture_open(void);
bool solar_scope_capture_arm(uint32_t rate);
/* 1 complete, 0 pending, -1 error. Copies a stopped, coherent DMA buffer. */
int solar_scope_capture_take(uint16_t *out, size_t count, uint32_t *actual_rate);
void solar_scope_capture_cancel(void);
void solar_scope_capture_close(void);
int solar_scope_capture_pin(void);
#ifdef __cplusplus
}
#endif
