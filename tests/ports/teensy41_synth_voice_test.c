/* Exercise the unchanged shared engine using a deterministic PCM sink. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "solar_os_memory.h"
static unsigned voice_allocations;
void *solar_os_memory_alloc(size_t n,solar_os_memory_class_t kind,const char *tag) {
    (void)tag;assert(kind==SOLAR_OS_MEMORY_INTERNAL_CRITICAL);void *p=malloc(n);if(p)++voice_allocations;return p;
}
void solar_os_memory_free(void *p){if(p){assert(voice_allocations);--voice_allocations;free(p);}}
#include <string.h>
#include "solar_os_synth_voice.h"

size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t length = strlen(src);
    if (size) { size_t n = length < size - 1 ? length : size - 1;
        memcpy(dst, src, n); dst[n] = 0; }
    return length;
}
static solar_os_synth_config_t renderer;
static solar_os_synth_status_t service;
esp_err_t solar_os_synth_start(const solar_os_synth_config_t *config) {
    assert(!service.running);
    renderer = *config;
    service.running = true;
    service.sample_rate = 44100;
    strlcpy(service.owner, config->owner, sizeof(service.owner));
    return ESP_OK;
}
esp_err_t solar_os_synth_stop(const char *owner) {
    assert(!strcmp(owner, service.owner));
    service.running = false;
    return ESP_OK;
}
void solar_os_synth_get_status(solar_os_synth_status_t *out) { *out = service; }
static int last_peak;
static void render(unsigned count) {
    int16_t samples[512];
    while (count--) {
        memset(samples, 0xa5, sizeof(samples));
        renderer.render(samples, 256, 44100, renderer.user);
        last_peak = 0;
        for (unsigned i=0; i<256; ++i) {
            assert(samples[i*2] == samples[i*2+1]);
            int value = samples[i*2] < 0 ? -samples[i*2] : samples[i*2];
            if (value > last_peak) last_peak = value;
        }
    }
}
int main(void) {
    const char *owner="test";
    solar_os_synth_voice_status_t voice;
    solar_os_synth_voice_get_status(&voice);
    solar_os_synth_voice_config_t config=voice.config;
    for (unsigned wave=0; wave<6; ++wave) {
        config.waveform=wave;
        config.oscillator2.waveform=SOLAR_OS_SYNTH_WAVE_SAW;
        config.oscillator2.mix_percent=50;
        config.oscillator2.detune_cents=7;
        config.filter.cutoff_hz=2400;
        config.filter.resonance_percent=40;
        assert(solar_os_synth_voice_configure(owner,&config)==ESP_OK);
        for (unsigned i=0; i<8; ++i)
            assert(solar_os_synth_voice_note_on(owner,220+i*40,100)==ESP_OK);
        render(40);
        solar_os_synth_voice_get_status(&voice);
        assert(voice.active_voices==8 && voice.pcm_peak>0);
        assert(solar_os_synth_voice_note_on("other",440,100)==ESP_ERR_INVALID_STATE);
        assert(solar_os_synth_voice_note_on(owner,700,100)==ESP_OK);
        render(3);
        solar_os_synth_voice_get_status(&voice);
        assert(voice.active_voices==8 && voice.stolen_voices>0);
        assert(solar_os_synth_voice_all_notes_off(owner)==ESP_OK);
        render(100);
        solar_os_synth_voice_get_status(&voice);
        assert(voice.active_voices==0 && last_peak==0);
        assert(solar_os_synth_voice_stop(owner)==ESP_OK);
    }
    config.attack_ms=10001;
    assert(solar_os_synth_voice_configure(owner,&config)==ESP_ERR_INVALID_ARG);
    assert(solar_os_synth_voice_note_on(owner,0,100)==ESP_ERR_INVALID_ARG);
    puts("PASS: six waveforms, eight voices, oscillator/filter, ownership, stealing, release and restart");
}
