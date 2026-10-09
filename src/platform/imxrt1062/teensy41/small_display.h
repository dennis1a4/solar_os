#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// One graphics owner at a time. The dashboard pauses while claimed.
bool sk_small_acquire(void);
uint8_t sk_gameboy_buttons(void);
bool sk_gameboy_keyboard_event(void);
void sk_gameboy_trace(char kind, uint8_t value);
void sk_gameboy_input_status(char *out, unsigned size);
void sk_gameboy_log(const char *text);
void sk_gameboy_status(char *out, unsigned size);
void sk_small_release(void);
bool sk_small_frame(const uint16_t *pixels);
#ifdef __cplusplus
}
#endif
