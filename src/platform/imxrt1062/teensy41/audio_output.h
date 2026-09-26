#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
bool sk_audio_cancelled(void);
esp_err_t sk_audio_output_start(uint8_t volume);
esp_err_t sk_audio_output_write(const int16_t *stereo, size_t frames);
esp_err_t sk_audio_output_finish(bool drain);
void sk_audio_output_status(void);
#ifdef __cplusplus
}
#endif
