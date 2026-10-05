#if SK_AUDIO_PLAYER
// File transport for the shared aplay app. Decode and SD access remain in the
// foreground console task; only PCM consumption happens in the audio ISR.
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include "solar_os_storage.h"
#include "solar_os_audio.h"
#include "solar_os_audio_codec.h"
#include "solar_os_audio_pcm.h"
#include "solar_os_memory.h"
#include "audio_output.h"
#define INPUT_SIZE 16384U
#define OUTPUT_SAMPLES 2048U
static uint32_t le32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1]<<8; }
static bool cancelled(const solar_os_audio_wav_options_t *o) {
    return sk_audio_cancelled() || (o && o->should_cancel && o->should_cancel(o->user));
}
static esp_err_t skip_id3(FILE *f) {
    uint8_t h[10];
    if (fread(h, 1, sizeof(h), f) < sizeof(h)) return ESP_ERR_NOT_SUPPORTED;
    if (memcmp(h, "ID3", 3)) return fseek(f, 0, SEEK_SET) ? ESP_FAIL : ESP_OK;
    if ((h[6] | h[7] | h[8] | h[9]) & 0x80) return ESP_ERR_NOT_SUPPORTED;
    uint32_t length = ((uint32_t)h[6]<<21) | ((uint32_t)h[7]<<14) | ((uint32_t)h[8]<<7) | h[9];
    length += 10 + ((h[3] == 4 && (h[5] & 0x10)) ? 10 : 0);
    return fseek(f, length, SEEK_SET) ? ESP_FAIL : ESP_OK;
}
static void format_info(solar_os_audio_wav_info_t *info, const solar_os_stream_audio_format_t *f) {
    info->sample_rate = f->sample_rate; info->channels = f->channels;
    info->bits_per_sample = 16; info->block_align = f->channels * 2;
}
esp_err_t solar_os_audio_get_mp3_info(const char *path, solar_os_audio_wav_info_t *info) {
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_FAIL;
    esp_err_t err = skip_id3(f);
    uint8_t buffer[1024];
    memset(info, 0, sizeof(*info));
    for (unsigned scanned = 0; err == ESP_OK && scanned < 65536; scanned += sizeof(buffer)-3) {
        size_t n = fread(buffer, 1, sizeof(buffer), f);
        solar_os_stream_audio_format_t format;
        if (solar_os_audio_mp3_probe(buffer, n, &format) == ESP_OK) {
            format_info(info, &format); fclose(f); return ESP_OK;
        }
        if (n < sizeof(buffer)) break;
        if (fseek(f, -3, SEEK_CUR)) break;
    }
    fclose(f);
    return ESP_ERR_NOT_SUPPORTED;
}
static esp_err_t wav_header(FILE *f, solar_os_audio_wav_info_t *info) {
    uint8_t header[16];
    memset(info, 0, sizeof(*info));
    if (fread(header, 1, 12, f) != 12 || memcmp(header, "RIFF", 4) || memcmp(header+8, "WAVE", 4))
        return ESP_ERR_NOT_SUPPORTED;
    bool format = false;
    // Bound malformed metadata scanning and all seek arithmetic.
    for (unsigned chunks = 0; chunks < 128; ++chunks) {
        if (fread(header, 1, 8, f) != 8) return ESP_ERR_NOT_SUPPORTED;
        uint32_t length = le32(header+4);
        if (length > INT32_MAX - 1) return ESP_ERR_NOT_SUPPORTED;
        if (!memcmp(header, "data", 4)) {
            if (!format || length % info->block_align) return ESP_ERR_NOT_SUPPORTED;
            info->data_bytes = length;
            info->duration_ms = (uint64_t)length * 1000 / info->block_align / info->sample_rate;
            return ESP_OK;
        }
        if (!memcmp(header, "fmt ", 4)) {
            if (length < 16 || fread(header, 1, 16, f) != 16 || le16(header) != 1) return ESP_ERR_NOT_SUPPORTED;
            unsigned channels = le16(header+2), bits = le16(header+14), align = le16(header+12);
            uint32_t rate = le32(header+4);
            if ((channels != 1 && channels != 2) || bits != 16 || align != channels*2 || rate < 8000 || rate > 96000)
                return ESP_ERR_NOT_SUPPORTED;
            info->sample_rate = rate; info->channels = channels; info->bits_per_sample = bits; info->block_align = align;
            format = true;
            length -= 16;
        }
        if (fseek(f, length + (length & 1), SEEK_CUR)) return ESP_FAIL;
    }
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t solar_os_audio_get_wav_info(const char *path, solar_os_audio_wav_info_t *info) {
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_FAIL;
    esp_err_t err = wav_header(f, info);
    fclose(f); return err;
}
static esp_err_t output_frame(solar_os_audio_s16_converter_t *converter,
    const int16_t *pcm, size_t frames, const solar_os_stream_audio_format_t *format,
    int16_t *output, const solar_os_audio_wav_options_t *options) {
    const solar_os_stream_audio_format_t target = {
        .sample_rate = 44100, .channels = 2, .bits_per_sample = 16,
        .sample_format = SOLAR_OS_STREAM_AUDIO_S16_LE
    };
    bool done = false;
    do {
        if (cancelled(options)) return ESP_ERR_TIMEOUT;
        size_t count;
        esp_err_t err = solar_os_audio_s16_convert(converter, pcm, frames, format, &target,
            output, OUTPUT_SAMPLES, &count, &done);
        if (err != ESP_OK) return err;
        err = sk_audio_output_write(output, count / 2);
        if (err != ESP_OK) return err;
        if (options && options->samples && count)
            options->samples(output, count, 2, options->user);
    } while (!done);
    return ESP_OK;
}
static esp_err_t playback_start(uint8_t volume, const solar_os_audio_wav_options_t *options) {
    // This port deliberately does not implement seeking.
    if (options && options->start_ms) return ESP_ERR_NOT_SUPPORTED;
    esp_err_t err=sk_audio_output_start(volume);
    if (err==ESP_OK && options && options->device) {
        solar_os_audio_device_info_t device={0};
        strcpy(device.id,"sgtl5000");
        device.capabilities=SOLAR_OS_AUDIO_DEVICE_CAP_OUTPUT|SOLAR_OS_AUDIO_DEVICE_CAP_VOLUME;
        device.native_format.sample_rate=44100;
        device.native_format.channels=2; device.native_format.bits_per_sample=16;
        device.native_format.sample_format=SOLAR_OS_STREAM_AUDIO_S16_LE;
        options->device(&device,options->user);
    }
    return err;
}
esp_err_t solar_os_audio_play_mp3(const char *path, uint8_t volume,
    const solar_os_audio_wav_options_t *options, solar_os_audio_wav_info_t *info) {
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_FAIL;
    solar_os_audio_mp3_decoder_t *decoder = NULL;
    uint8_t *input = solar_os_memory_alloc(INPUT_SIZE, SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "mp3.input");
    int16_t *pcm = solar_os_memory_alloc(SOLAR_OS_AUDIO_MP3_MAX_PCM_SAMPLES*2, SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "mp3.pcm");
    int16_t *output = solar_os_memory_alloc(OUTPUT_SAMPLES*2, SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "mp3.output");
    esp_err_t err = input && pcm && output ? solar_os_audio_mp3_decoder_create(&decoder) : ESP_ERR_NO_MEM;
    if (err == ESP_OK) err = skip_id3(f);
    if (err == ESP_OK) err = playback_start(volume, options);
    solar_os_audio_s16_converter_t converter = {0};
    size_t used = 0;
    bool eof = false, have_audio = false;
    uint64_t duration_us = 0;
    memset(info, 0, sizeof(*info));
    while (err == ESP_OK) {
        if (cancelled(options)) { err = ESP_ERR_TIMEOUT; break; }
        if (!eof && used < INPUT_SIZE) {
            size_t n = fread(input+used, 1, INPUT_SIZE-used, f);
            if (ferror(f)) { err = ESP_FAIL; break; }
            used += n; eof = feof(f);
        }
        if (!used) break;
        size_t consumed = 0;
        solar_os_audio_decoded_frame_t frame;
        err = solar_os_audio_mp3_decode(decoder, input, used, &consumed, pcm,
            SOLAR_OS_AUDIO_MP3_MAX_PCM_SAMPLES, &frame);
        if (err != ESP_OK) break;
        if (frame.frames) {
            have_audio = true;
            format_info(info, &frame.format);
            duration_us += (uint64_t)frame.frames * 1000000 / frame.format.sample_rate;
            err = output_frame(&converter, pcm, frame.frames, &frame.format, output, options);
        }
        if (!consumed) { if (!eof) err = ESP_ERR_NOT_SUPPORTED; break; }
        info->data_bytes += consumed;
        used -= consumed; memmove(input, input+consumed, used);
    }
    if (err == ESP_OK && !have_audio) err = ESP_ERR_NOT_SUPPORTED;
    esp_err_t drained = sk_audio_output_finish(err == ESP_OK);
    if (err == ESP_OK) err = drained;
    info->duration_ms = duration_us / 1000;
    solar_os_audio_mp3_decoder_destroy(decoder);
    solar_os_memory_free(input); solar_os_memory_free(pcm); solar_os_memory_free(output);
    fclose(f);
    return err;
}
esp_err_t solar_os_audio_play_wav(const char *path, uint8_t volume,
    const solar_os_audio_wav_options_t *options, solar_os_audio_wav_info_t *info) {
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_FAIL;
    esp_err_t err = wav_header(f, info);
    int16_t *pcm = solar_os_memory_alloc(4096, SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "wav.pcm");
    int16_t *output = solar_os_memory_alloc(OUTPUT_SAMPLES*2, SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "wav.output");
    if (!pcm || !output) err = ESP_ERR_NO_MEM;
    if (err == ESP_OK) err = playback_start(volume, options);
    uint32_t remaining = info->data_bytes;
    solar_os_audio_s16_converter_t converter = {0};
    solar_os_stream_audio_format_t format = {.sample_rate=info->sample_rate,
        .channels=info->channels, .bits_per_sample=16, .sample_format=SOLAR_OS_STREAM_AUDIO_S16_LE};
    while (err == ESP_OK && remaining) {
        if (cancelled(options)) { err = ESP_ERR_TIMEOUT; break; }
        size_t n = remaining < 4096 ? remaining : 4096;
        if (fread(pcm, 1, n, f) != n) { err = ESP_FAIL; break; }
        remaining -= n;
        err = output_frame(&converter, pcm, n/info->block_align, &format, output, options);
    }
    esp_err_t drained = sk_audio_output_finish(err == ESP_OK);
    if (err == ESP_OK) err = drained;
    solar_os_memory_free(pcm); solar_os_memory_free(output); fclose(f);
    return err;
}
static void put32(uint8_t *p, uint32_t n) {
    for (unsigned i=0; i<4; ++i) p[i] = n >> (i*8);
}
static bool write_wav_header(FILE *f, uint32_t bytes) {
    uint8_t h[44] = {0};
    memcpy(h, "RIFF", 4); put32(h+4, bytes+36); memcpy(h+8, "WAVEfmt ", 8);
    put32(h+16, 16); h[20]=1; h[22]=1;
    put32(h+24, 44100); put32(h+28, 88200); h[32]=2; h[34]=16;
    memcpy(h+36, "data", 4); put32(h+40, bytes);
    return fseek(f, 0, SEEK_SET) == 0 && fwrite(h, 1, sizeof(h), f) == sizeof(h);
}
esp_err_t solar_os_audio_record_wav(const char *path, uint32_t duration,
    const solar_os_audio_wav_options_t *options, solar_os_audio_wav_info_t *info) {
    memset(info, 0, sizeof(*info));
    if (options && options->capture_stream && strcmp(options->capture_stream, "mic"))
        return ESP_ERR_NOT_SUPPORTED;
    struct stat st;
    if (stat(path, &st) == 0) return ESP_ERR_INVALID_STATE;
    if (errno != ENOENT) return ESP_FAIL;
    int16_t *buffer = solar_os_memory_alloc(4096, SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "mic.record");
    if (!buffer) return ESP_ERR_NO_MEM;
    FILE *f = fopen(path, "wb");
    if (!f) { solar_os_memory_free(buffer); return ESP_FAIL; }
    info->sample_rate=44100; info->channels=1; info->bits_per_sample=16; info->block_align=2;
    // SD explicitly opts into the bounded PSRAM spool. Short diagnostics on
    // RAMFS and flash keep their existing small capture path.
    const bool buffered = strncmp(path, "/sd/", 4) == 0;
    esp_err_t err = write_wav_header(f, 0) ?
        (buffered ? sk_audio_capture_start_buffered() : sk_audio_capture_start()) : ESP_FAIL;
    // Unbounded interactive recording stops at the existing one-hour limit.
    uint64_t wanted = (uint64_t)(duration ? duration : SOLAR_OS_AUDIO_WAV_MAX_MS)*44100/1000;
    while (err == ESP_OK && info->data_bytes/2 < wanted) {
        if (cancelled(options)) { err = ESP_ERR_TIMEOUT; break; }
        size_t frames = 0;
        err = sk_audio_capture_read(buffer, 2048, &frames);
        if (err != ESP_OK) break;
        uint64_t remaining = wanted - info->data_bytes/2;
        if (frames > remaining) frames = remaining;
        size_t n = fwrite(buffer, 2, frames, f);
        info->data_bytes += n*2;
        if (n != frames) { err=ESP_FAIL; break; }
    }
    if (sk_audio_capture_stop() && err == ESP_OK) err=ESP_FAIL;
    info->duration_ms=(uint64_t)info->data_bytes*1000/88200;
    // Even cancellation leaves a properly sized, playable partial WAV.
    if (!write_wav_header(f, info->data_bytes) || solar_os_storage_sync_file(f) != ESP_OK) err=ESP_FAIL;
    if (fclose(f) != 0) err=ESP_FAIL;
    solar_os_memory_free(buffer);
    return err;
}
#endif
