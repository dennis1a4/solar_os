#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
bool sk_audio_cancelled(void);
// A file-decoder worker must not poll console input or take its owner's gate.
void sk_audio_worker_begin(bool (*cancel)(void *), bool (*pause)(void *), void *user);
void sk_audio_worker_end(void);
bool sk_audio_worker_active(void);
// Whether the SGTL5000 initialized successfully at boot.
bool sk_audio_output_ready(void);
esp_err_t sk_audio_output_start(uint8_t volume);
esp_err_t sk_audio_output_write(const int16_t *stereo, size_t frames);
esp_err_t sk_audio_output_finish(bool drain);
void sk_audio_output_status(void);
esp_err_t sk_audio_capture_start(void);
esp_err_t sk_audio_capture_start_buffered(void);
bool sk_audio_capture_active(void);
esp_err_t sk_audio_capture_read(int16_t *mono, size_t capacity, size_t *frames);
uint32_t sk_audio_capture_stop(void);
#ifdef __cplusplus
}
#endif

#if SK_SYNTH
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t sk_audio_synth_write(const int16_t *stereo, size_t frames, const volatile bool *stop);
esp_err_t sk_audio_output_volume(uint8_t volume);
uint32_t sk_audio_output_underruns(void);
#ifdef __cplusplus
}
#endif
#endif
