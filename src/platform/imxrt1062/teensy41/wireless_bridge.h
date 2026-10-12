#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Called by the sole lwIP owner task; complete Ethernet frames, max 1514. */
bool sk_wireless_wifi_available(void);
bool sk_wireless_wifi_pending(void);
unsigned sk_wireless_wifi_command(unsigned op, const void *, size_t, uint8_t *, size_t, size_t *);
unsigned sk_wireless_wifi_tx(const uint8_t *, size_t);
unsigned sk_wireless_wifi_rx(uint8_t *, size_t, size_t *);
void sk_wireless_init(void);
bool sk_wireless_shutdown(void);
int sk_wireless_key_read(void);
void sk_wireless_key_boundary(void);
bool sk_wireless_audio_selected(void);
bool sk_wireless_audio_ready(void);
esp_err_t sk_wireless_audio_start(uint8_t volume);
esp_err_t sk_wireless_audio_write(const int16_t *, size_t frames);
esp_err_t sk_wireless_audio_finish(bool drain);
esp_err_t sk_wireless_audio_synth_write(const int16_t *, size_t,
                                        const volatile bool *);
esp_err_t sk_wireless_audio_volume(uint8_t);
#ifdef __cplusplus
}
#endif
