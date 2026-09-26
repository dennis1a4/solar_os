#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "solar_os_audio.h"
#include "solar_os_memory.h"
#include "audio_output.h"
static size_t frames_written, live;
static int fail_after = -1;
static bool stop, started, sink_failure;
static int peak;
void *solar_os_memory_alloc(size_t size, solar_os_memory_class_t cls, const char *tag) {
    (void)cls; (void)tag;
    if (fail_after == 0) return NULL;
    if (fail_after > 0) --fail_after;
    void *p = malloc(size); if (p) ++live; return p;
}
void solar_os_memory_free(void *p) { if (p) { assert(live); --live; free(p); } }
bool sk_audio_cancelled(void) { return stop; }
esp_err_t sk_audio_output_start(uint8_t volume) {
    assert(volume == 20); frames_written = 0; peak = 0; started = true; return ESP_OK;
}
esp_err_t sk_audio_output_write(const int16_t *pcm, size_t frames) {
    assert(started);
    if (sink_failure) return ESP_FAIL;
    for (size_t i = 0; i < frames*2; ++i) {
        int n = pcm[i] < 0 ? -(int)pcm[i] : pcm[i]; if (n > peak) peak = n;
    }
    frames_written += frames; return ESP_OK;
}
esp_err_t sk_audio_output_finish(bool drain) { (void)drain; started = false; return ESP_OK; }
static unsigned capture_count;
esp_err_t sk_audio_capture_start(void) { capture_count=0; return ESP_OK; }
esp_err_t sk_audio_capture_read(int16_t *data, size_t capacity, size_t *frames) {
    *frames = capacity < 512 ? capacity : 512;
    for (size_t i=0; i<*frames; ++i) data[i] = (++capture_count % 100 < 50) ? 1000 : -1000;
    return ESP_OK;
}
uint32_t sk_audio_capture_stop(void) { return 0; }
esp_err_t solar_os_storage_sync_file(FILE *f) { return fflush(f) == 0 ? ESP_OK : ESP_FAIL; }
int main(int argc, char **argv) {
    assert(argc == 4);
    solar_os_audio_wav_info_t info;
    assert(solar_os_audio_get_mp3_info(argv[1], &info) == ESP_OK);
    assert(info.sample_rate == 44100 && info.channels == 2);
    assert(solar_os_audio_play_mp3(argv[1], 20, NULL, &info) == ESP_OK);
    assert(frames_written >= 44100 && frames_written < 50000 && peak > 100);
    assert(!live);
    assert(solar_os_audio_get_mp3_info(argv[2], &info) == ESP_OK);
    assert(info.sample_rate == 48000 && info.channels == 1);
    assert(solar_os_audio_play_mp3(argv[2], 20, NULL, &info) == ESP_OK);
    assert(frames_written >= 44100 && frames_written < 50000 && peak > 100);
    assert(!live);
    assert(solar_os_audio_get_wav_info(argv[3], &info) == ESP_OK);
    assert(info.sample_rate == 22050 && info.channels == 1);
    assert(solar_os_audio_play_wav(argv[3], 20, NULL, &info) == ESP_OK);
    assert(frames_written == 44100 && peak > 100 && !live);
    sink_failure = true;
    assert(solar_os_audio_play_mp3(argv[1], 20, NULL, &info) == ESP_FAIL);
    assert(!live && !started);
    sink_failure = false;
    stop = true;
    assert(solar_os_audio_play_mp3(argv[1], 20, NULL, &info) == ESP_ERR_TIMEOUT);
    assert(!live && !started);
    stop = false;
    for (fail_after = 0; fail_after < 3; ) {
        int next = fail_after + 1;
        assert(solar_os_audio_play_mp3(argv[1], 20, NULL, &info) == ESP_ERR_NO_MEM);
        assert(!live && !started); fail_after = next;
    }
    fail_after = -1;
    // A truncated file may have a valid header, but playback must fail cleanly.
    char damaged[512]; snprintf(damaged, sizeof(damaged), "%s.truncated", argv[3]);
    FILE *in = fopen(argv[3], "rb"), *out = fopen(damaged, "wb");
    assert(in && out);
    char bytes[256]; size_t n = fread(bytes, 1, sizeof(bytes), in);
    assert(fwrite(bytes, 1, n, out) == n); fclose(in); fclose(out);
    assert(solar_os_audio_play_wav(damaged, 20, NULL, &info) == ESP_FAIL);
    assert(!live && !started);
    out = fopen(damaged, "wb"); assert(out); fputs("not an MP3 or WAV", out); fclose(out);
    assert(solar_os_audio_get_mp3_info(damaged, &info) != ESP_OK);
    assert(solar_os_audio_get_wav_info(damaged, &info) != ESP_OK);
    assert(solar_os_audio_play_mp3(damaged, 20, NULL, &info) != ESP_OK);
    assert(!live && !started);
    assert(solar_os_audio_get_wav_info(argv[1], &info) != ESP_OK);
    assert(solar_os_audio_play_wav("/no/such/file.wav", 20, NULL, &info) != ESP_OK);
    char recording[512]; snprintf(recording, sizeof(recording), "%s.recorded.wav", argv[3]);
    assert(solar_os_audio_record_wav(recording, 100, NULL, &info) == ESP_OK);
    assert(info.data_bytes == 8820 && info.duration_ms == 100 && !live);
    assert(solar_os_audio_get_wav_info(recording, &info) == ESP_OK);
    assert(info.channels == 1 && info.sample_rate == 44100 && info.data_bytes == 8820);
    assert(solar_os_audio_record_wav(recording, 100, NULL, &info) == ESP_ERR_INVALID_STATE);
    assert(!live);
    puts("Teensy audio files: real MP3/WAV decode, resampling, cancellation and allocation cleanup passed");
}
