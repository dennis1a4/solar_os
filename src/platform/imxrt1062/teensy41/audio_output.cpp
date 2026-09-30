#if SK_AUDIO_PLAYER
#include <arduino_freertos.h>
#include <Audio.h>
#include <math.h>
#include "platform.h"
#include "audio_output.h"
extern "C" {
#include "solar_os_shell_commands.h"
#include "solar_os_shell_io.h"
#include "solar_os_audio.h"
#include "solar_os_shell.h"
}

// One foreground producer, AudioStream ISR consumer. DMA only sees the Audio
// library's internal blocks; this ring is in OCRAM and never allocated in PSRAM.
static constexpr unsigned slots = 32;
DMAMEM static int16_t pcm[slots][AUDIO_BLOCK_SAMPLES * 2];
static volatile unsigned head, tail;
static volatile uint32_t played, underruns;
static volatile bool consuming, tone_on;
static bool ready;
#if SK_CLOCK
static volatile bool clock_tone_on;
static volatile uint32_t clock_tone_until;
extern "C" void sk_clock_alarm_sound(bool on) {
    // Independent alarm gate: never reset a player, queue, volume or test tone.
    if (!on) { clock_tone_on=false; return; }
    if (!ready || consuming || head!=tail || tone_on) return;
    clock_tone_until=millis()+1000;
    __DMB(); clock_tone_on=true;
}
#endif
static unsigned partial;
static volatile uint32_t tone_until, tone_blocks;
class StereoSource : public AudioStream {
public:
    StereoSource() : AudioStream(0, nullptr) {}
    void update() override {
        bool clock_sound=false;
#if SK_CLOCK
        if (clock_tone_on && int32_t(millis()-clock_tone_until)>=0) clock_tone_on=false;
        clock_sound=clock_tone_on && !consuming && head==tail && !tone_on;
#endif
        if (head == tail && !tone_on && !clock_sound) {
            if (consuming) ++underruns;
            return;
        }
        audio_block_t *l = allocate(), *r = allocate();
        if (!l || !r) {
            if (l) release(l);
            if (r) release(r);
            return;
        }
        if (tone_on || clock_sound) {
            ++tone_blocks;
            static float phase;
            if (int32_t(millis() - tone_until) >= 0) tone_on = false;
            for (unsigned i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
                l->data[i] = r->data[i] = int16_t(sinf(phase) * 1600);
                phase += 2.0f * 3.14159265f * 440.0f / AUDIO_SAMPLE_RATE_EXACT;
                if (phase >= 2.0f * 3.14159265f) phase -= 2.0f * 3.14159265f;
            }
        } else {
            const unsigned t = tail;
            for (unsigned i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
                l->data[i] = pcm[t][i * 2];
                r->data[i] = pcm[t][i * 2 + 1];
            }
            __DMB();
            tail = (t + 1) % slots;
            ++played;
        }
        transmit(l, 0); transmit(r, 1);
        release(l); release(r);
    }
};
static StereoSource source;
static AudioOutputI2S output;
static AudioConnection left(source, 0, output, 0), right(source, 1, output, 1);
static AudioControlSGTL5000 codec;
static constexpr unsigned capture_slots = 128;
DMAMEM static int16_t captured[capture_slots][AUDIO_BLOCK_SAMPLES];
static volatile unsigned capture_head, capture_tail;
static volatile bool capture_enabled;
static bool capture_test, test_tone_started;
static volatile uint32_t capture_drops, capture_blocks;
class MicCapture : public AudioStream {
    audio_block_t *inputs[1];
public:
    MicCapture() : AudioStream(1, inputs) {}
    void update() override {
        audio_block_t *block = receiveReadOnly();
        if (!block) return;
        if (capture_enabled) {
            unsigned next = (capture_head + 1) % capture_slots;
            if (next == capture_tail) ++capture_drops;
            else {
                memcpy(captured[capture_head], block->data, sizeof(block->data));
                __DMB(); capture_head = next; ++capture_blocks;
            }
        }
        release(block);
    }
};
static AudioInputI2S input;
static MicCapture capture;
static AudioConnection mic_connection(input, 0, capture, 0);
extern "C" esp_err_t sk_audio_capture_start() {
    if (!ready) return ESP_ERR_NOT_FOUND;
    sk_audio_output_finish(false);
    if (!sk_i2c_lock(0)) return ESP_ERR_TIMEOUT;
    bool ok = codec.inputSelect(AUDIO_INPUT_MIC) && codec.micGain(20);
    sk_i2c_unlock(0);
    if (!ok) return ESP_FAIL;
    AudioNoInterrupts();
    test_tone_started = false;
    capture_head = capture_tail = 0;
    capture_drops = capture_blocks = 0;
    capture_enabled = true;
    AudioInterrupts();
    return ESP_OK;
}
extern "C" esp_err_t sk_audio_capture_read(int16_t *mono, size_t capacity, size_t *frames) {
    *frames = 0;
    uint32_t started = millis();
    while (capture_head == capture_tail) {
        if (sk_audio_cancelled()) return ESP_ERR_TIMEOUT;
        if (millis() - started > 1000) return ESP_FAIL;
        vTaskDelay(1);
    }
    if (capture_test && !test_tone_started && capture_blocks >= 345) {
        tone_until = millis() + 1000;
        __DMB(); tone_on = true; test_tone_started = true;
    }
    if (capture_drops) return ESP_FAIL;
    while (capture_head != capture_tail && *frames + AUDIO_BLOCK_SAMPLES <= capacity) {
        unsigned t = capture_tail;
        memcpy(mono + *frames, captured[t], sizeof(captured[t]));
        __DMB(); capture_tail = (t + 1) % capture_slots;
        *frames += AUDIO_BLOCK_SAMPLES;
    }
    return ESP_OK;
}
extern "C" uint32_t sk_audio_capture_stop() {
    AudioNoInterrupts();
    capture_enabled = false;
    if (capture_test) tone_on = false;
    AudioInterrupts();
    return capture_drops;
}

void sk_audio_player_begin() {
    AudioMemory(16);
    if (!sk_i2c_lock(0)) return;
    ready = codec.enable();
    if (ready) {
        codec.volume(0.2f);
        codec.muteLineout();
    }
    sk_i2c_unlock(0);
    sk_console_print(ready ? "Audio shield: SGTL5000 ready, headphones at 20%\r\n" :
                            "Audio shield: SGTL5000 not detected\r\n");
}
#if SK_LCD_CONSOLE
extern "C" bool sk_console_poll_cancel(bool);
extern "C" bool sk_audio_owner_connected();
#endif
extern "C" bool sk_audio_cancelled() {
#if SK_LCD_CONSOLE
    return sk_console_poll_cancel(true);
#else
    if (!Serial) return true;
    bool stop = false;
    while (Serial.available()) {
        int ch = Serial.read();
        stop |= ch == 3 || ch == 27 || ch == 29;
    }
    return stop;
#endif
}
extern "C" esp_err_t sk_audio_output_start(uint8_t volume) {
    if (!ready) return ESP_ERR_NOT_FOUND;
    sk_audio_output_finish(false);
    if (!sk_i2c_lock(0)) return ESP_ERR_TIMEOUT;
    const bool configured = codec.volume((volume == 255 ? 20 : volume) / 100.0f);
    sk_i2c_unlock(0);
    played = underruns = tone_blocks = 0;
    return configured ? ESP_OK : ESP_FAIL;
}
extern "C" esp_err_t sk_audio_output_write(const int16_t *data, size_t frames) {
    while (frames) {
        uint32_t started = millis();
        while ((head + 1) % slots == tail) {
            if (sk_audio_cancelled()) return ESP_ERR_TIMEOUT;
            if (millis() - started > 1000) return ESP_FAIL;
            vTaskDelay(1);
        }
        const size_t count = min(frames, size_t(AUDIO_BLOCK_SAMPLES - partial));
        memcpy(pcm[head] + partial * 2, data, count * 4);
        data += count * 2; frames -= count; partial += count;
        if (partial == AUDIO_BLOCK_SAMPLES) {
            __DMB();
            head = (head + 1) % slots;
            consuming = true;
            partial = 0;
        }
    }
    return ESP_OK;
}
#if SK_SYNTH
extern "C" esp_err_t sk_audio_output_volume(uint8_t volume) {
    if (volume > 100) return ESP_ERR_INVALID_ARG;
    if (!ready) return ESP_ERR_NOT_FOUND;
    if (!sk_i2c_lock(0)) return ESP_ERR_TIMEOUT;
    bool ok = codec.volume(volume / 100.0f);
    sk_i2c_unlock(0);
    return ok ? ESP_OK : ESP_FAIL;
}
extern "C" uint32_t sk_audio_output_underruns() { return underruns; }
// Synth has a foreground input owner. Never consume Serial from this worker.
// Limit queued audio to four blocks (~12 ms), instead of the player's 90 ms.
extern "C" esp_err_t sk_audio_synth_write(const int16_t *data, size_t frames,
                                          const volatile bool *stop) {
    while (frames) {
        uint32_t started = millis();
        while ((head + slots - tail) % slots >= 4) {
            if (*stop) return ESP_ERR_TIMEOUT;
#if SK_LCD_CONSOLE
            if (!sk_audio_owner_connected()) return ESP_FAIL;
#else
            if (!Serial) return ESP_FAIL;
#endif
            if (millis() - started > 1000) return ESP_FAIL;
            vTaskDelay(1);
        }
        if (*stop) return ESP_ERR_TIMEOUT;
#if SK_LCD_CONSOLE
        if (!sk_audio_owner_connected()) return ESP_FAIL;
#else
        if (!Serial) return ESP_FAIL;
#endif
        const size_t count = min(frames, size_t(AUDIO_BLOCK_SAMPLES - partial));
        memcpy(pcm[head] + partial * 2, data, count * 4);
        data += count * 2; frames -= count; partial += count;
        if (partial == AUDIO_BLOCK_SAMPLES) {
            __DMB(); head = (head + 1) % slots;
            consuming = true; partial = 0;
        }
    }
    return ESP_OK;
}
#endif
extern "C" esp_err_t sk_audio_output_finish(bool drain) {
    esp_err_t result = ESP_OK;
    consuming = false; // The producer is finished; an empty tail is no underrun.
    if (drain && partial) {
        memset(pcm[head] + partial * 2, 0, (AUDIO_BLOCK_SAMPLES - partial) * 4);
        __DMB(); head = (head + 1) % slots; partial = 0;
    }
    const uint32_t started = millis();
    while (drain && head != tail) {
        if (sk_audio_cancelled()) { result = ESP_ERR_TIMEOUT; break; }
        if (millis() - started > 1000) { result = ESP_FAIL; break; }
        vTaskDelay(1);
    }
    // Two I2S blocks can remain downstream of the source.
    if (drain && result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(10));
    AudioNoInterrupts();
    consuming = tone_on = false;
    head = tail = partial = 0;
    AudioInterrupts();
    return result;
}
void sk_audio_player_tone(bool on) {
    if (!ready) { sk_console_print("Audio unavailable\r\n"); return; }
    if (sk_audio_output_start(20) != ESP_OK) return;
    tone_until = millis() + 1000;
    __DMB();
    tone_on = on;
}
extern "C" void sk_audio_output_status() {
    sk_console_printf("Audio: SGTL5000=%s rate=44100 stereo blocks=%lu underruns=%lu\r\n",
        ready ? "ready" : "missing", (unsigned long)played, (unsigned long)underruns);
    sk_console_printf("Capture: mic gain=20dB blocks=%lu overruns=%lu\r\n",
        (unsigned long)capture_blocks, (unsigned long)capture_drops);
}
extern "C" void solar_os_shell_cmd_audio(solar_os_context_t *ctx, int argc, char **argv) {
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "status"))) {
        sk_audio_output_status();
    } else if (argc == 2 && !strcmp(argv[1], "tone")) {
        sk_audio_player_tone(true);
        solar_os_shell_io_writeln(solar_os_context_shell_io(ctx), ready ?
            "440 Hz test tone for one second, headphones at 20%" : "Audio shield missing");
    } else if (argc == 3 && !strcmp(argv[1], "mictest")) {
        char path[160];
        auto *io = solar_os_context_shell_io(ctx);
        if (solar_os_shell_resolve_path(ctx, argv[2], path, sizeof(path)) != ESP_OK) return;
        solar_os_shell_io_writeln(io, "Recording 4 seconds; a one-second tone follows one second of silence.");
        esp_err_t err = sk_audio_output_start(30);
        solar_os_audio_wav_info_t info{};
        if (err == ESP_OK) {
            capture_test = true;
            err = solar_os_audio_record_wav(path, 4000, nullptr, &info);
            capture_test = false;
        }
        solar_os_shell_io_printf(io, "Mic test: %s, %lu bytes, %lu ms\n", esp_err_to_name(err),
            (unsigned long)info.data_bytes, (unsigned long)info.duration_ms);
        solar_os_shell_io_printf(io, "Mic test tone: started=%u blocks=%lu\n",
            unsigned(test_tone_started), (unsigned long)tone_blocks);
    } else if (argc == 2 && !strcmp(argv[1], "off")) {
        sk_audio_player_tone(false);
    } else {
        solar_os_shell_io_writeln(solar_os_context_shell_io(ctx), "usage: audio [status|tone|off|mictest new.wav]");
    }
}
#endif
