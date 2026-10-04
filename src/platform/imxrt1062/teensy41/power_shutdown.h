#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if SK_HW_RESOURCES && SK_LCD_CONSOLE
void sk_power_init(void);
bool sk_power_requested(void);
bool sk_power_cleaning(void);
bool sk_power_interrupt_due(void);
uint32_t sk_power_generation(void);
const char *sk_power_status(void);
#else
static inline bool sk_power_requested(void) { return false; }
static inline bool sk_power_cleaning(void) { return false; }
static inline bool sk_power_interrupt_due(void) { return false; }
static inline uint32_t sk_power_generation(void) { return 0; }
#endif
#ifdef __cplusplus
}
#endif
