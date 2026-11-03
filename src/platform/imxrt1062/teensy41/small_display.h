#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    bool active, playing, paused;
    uint32_t elapsed_ms, total_ms;
    unsigned volume;
    char track[53];
} sk_small_music_t;
void sk_small_music_publish(const sk_small_music_t *status);
// A single pending play/pause request, consumed by the player app's own task.
bool sk_small_music_take_toggle(void);
bool sk_small_page(unsigned page);
void sk_small_input_poll(void);
void sk_small_input_stop(void);
// Debounced physical inputs (never consume keyboard events). Sources are
// 0=on-board buttons, 1=external NES. Existing apps use their combined mask.
unsigned sk_small_controller_count(void);
// Atomic pair snapshot, indexed as above; disabled sources return zero.
void sk_small_controller_snapshot(uint8_t buttons[2]);
uint8_t sk_small_controller_buttons(unsigned controller);
uint8_t sk_small_buttons(void);
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
#if SK_DMM
#include "dmm_model.h"
bool sk_small_dmm_frame(const DmmModel *view,bool demo);
#endif
#endif
