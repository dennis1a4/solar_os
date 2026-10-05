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
#include "solar_os_memory.h"
}

// One foreground producer, AudioStream ISR consumer. DMA only sees the Audio
// library's internal blocks. Allocate CPU rings on demand in internal RAM;
// detach them with the AudioStream interrupt disabled, then free outside it.
static constexpr unsigned slots = 32;
using PlaybackBlock = int16_t[AUDIO_BLOCK_SAMPLES * 2];
static PlaybackBlock *pcm;
static volatile unsigned head, tail;
static volatile uint32_t played, underruns;
static volatile bool consuming, tone_on;
static bool ready;
static TaskHandle_t file_worker;
static bool (*file_cancel)(void *), (*file_pause)(void *);
static void *file_user;
static volatile bool output_paused;
static uint32_t file_stack_free;
extern "C" void sk_audio_worker_begin(bool (*cancel)(void *), bool (*pause)(void *), void *user) {
    file_cancel=cancel; file_pause=pause; file_user=user;
    file_worker=xTaskGetCurrentTaskHandle();
}
extern "C" void sk_audio_worker_end() {
    file_stack_free=uxTaskGetStackHighWaterMark(nullptr)*sizeof(StackType_t);
    output_paused=false; file_worker=nullptr;
    file_cancel=file_pause=nullptr; file_user=nullptr;
}
extern "C" bool sk_audio_worker_active() { return file_worker != nullptr; }
static bool file_worker_context() {
    return file_worker && file_worker==xTaskGetCurrentTaskHandle();
}
static bool output_wait_ready(uint32_t *deadline_start) {
    if (!file_worker_context() || !file_pause || !file_pause(file_user)) return true;
    output_paused=true;
    while (file_pause(file_user)) {
        if (sk_audio_cancelled()) { output_paused=false; return false; }
        vTaskDelay(1);
    }
    output_paused=false;
    if (deadline_start) *deadline_start=millis();
    return !sk_audio_cancelled();
}
// Folder playback decodes ahead in PSRAM. A short priority-3 feeder keeps the
// internal ISR ring supplied while an editor redraw or SD write blocks decoding.
static constexpr unsigned file_slots=256;
static PlaybackBlock *file_pcm;
static volatile unsigned file_head,file_tail,file_peak;
static unsigned file_partial;
static volatile bool file_eof,file_primed,file_feed_stop;
static StaticTask_t file_feed_tcb;
static StackType_t *file_feed_stack;
static TaskHandle_t file_feed_task;
static uint32_t file_feed_stack_free;
static void file_feed_once() {
    output_paused=file_pause && file_pause(file_user);
    if (output_paused) return;
    if (!file_primed) {
        if (!file_eof && (file_head+file_slots-file_tail)%file_slots<file_slots/2) return;
        file_primed=true;
    }
    while (file_head!=file_tail && (head+1)%slots!=tail) {
        const unsigned t=file_tail,h=head;
        memcpy(pcm[h],file_pcm[t],sizeof(PlaybackBlock));
        __DMB(); head=(h+1)%slots; file_tail=(t+1)%file_slots;
        consuming=true;
    }
    if (file_eof && file_head==file_tail) consuming=false;
}
static void file_feed(void *) {
    while (!file_feed_stop) { file_feed_once(); vTaskDelay(1); }
    for (;;) vTaskSuspend(nullptr);
}
static void file_feed_release() {
    file_feed_stop=true;
    if (file_feed_task) {
        while (eTaskGetState(file_feed_task)!=eSuspended) vTaskDelay(1);
        file_feed_stack_free=uxTaskGetStackHighWaterMark(file_feed_task)*sizeof(StackType_t);
        vTaskDelete(file_feed_task); file_feed_task=nullptr;
    }
    solar_os_memory_free(file_pcm); file_pcm=nullptr;
    solar_os_memory_free(file_feed_stack); file_feed_stack=nullptr;
}
static esp_err_t file_output_write(const int16_t *data,size_t frames) {
    while (frames) {
        if (!output_wait_ready(nullptr)) return ESP_ERR_TIMEOUT;
        uint32_t started=millis();
        while ((file_head+1)%file_slots==file_tail) {
            if (!output_wait_ready(&started) || sk_audio_cancelled()) return ESP_ERR_TIMEOUT;
            if (millis()-started>1000) return ESP_FAIL;
            vTaskDelay(1);
        }
        const size_t count=min(frames,size_t(AUDIO_BLOCK_SAMPLES-file_partial));
        memcpy(file_pcm[file_head]+file_partial*2,data,count*4);
        data+=count*2;frames-=count;file_partial+=count;
        if (file_partial==AUDIO_BLOCK_SAMPLES) {
            __DMB(); file_head=(file_head+1)%file_slots; file_partial=0;
            unsigned used=(file_head+file_slots-file_tail)%file_slots;
            if(used>file_peak)file_peak=used;
        }
    }
    return ESP_OK;
}
// Console gate serializes diagnostics with app admission across all consoles.
static bool diagnostic_busy;
static bool monitor_capture;
extern "C" bool sk_audio_diagnostic_busy() { return diagnostic_busy; }
#if SK_LCD_CONSOLE
extern "C" bool sk_console_audio_busy();
extern "C" bool sk_console_recorder_busy();
#endif
static bool diagnostic_allowed() {
#if SK_LCD_CONSOLE
    if (sk_console_audio_busy()) return false;
#endif
    return !diagnostic_busy;
}
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
        if (output_paused) return;
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
using CaptureBlock = int16_t[AUDIO_BLOCK_SAMPLES];
static CaptureBlock *captured;
static volatile unsigned capture_head, capture_tail;
static volatile bool capture_enabled;
static bool capture_test, test_tone_started;
static volatile uint32_t capture_drops, capture_blocks;
// SD writes and the console gate can stall the foreground. A priority-3 feeder
// only copies PCM; it never touches files, codecs, consoles or allocation APIs.
// DMA and the AudioStream ISR continue to use internal memory exclusively.
static constexpr unsigned spool_slots = 1024; // 256 KiB, ~2.97 seconds
static CaptureBlock *spool;
static volatile unsigned spool_head, spool_tail, spool_peak;
static StaticTask_t feeder_tcb;
static StackType_t *feeder_stack;
static TaskHandle_t feeder;
static volatile bool feeder_stop;
static uint32_t feeder_stack_free;
static void capture_feed_once() {
    while (capture_head != capture_tail) {
        const unsigned h = spool_head, next = (h + 1) % spool_slots;
        if (next == spool_tail) break; // Internal ring absorbs the last ~0.37 s.
        const unsigned t = capture_tail;
        memcpy(spool[h], captured[t], sizeof(CaptureBlock));
        __DMB();
        capture_tail = (t + 1) % capture_slots;
        spool_head = next;
        const unsigned used = (next + spool_slots - spool_tail) % spool_slots;
        if (used > spool_peak) spool_peak = used;
    }
}
static void capture_feeder(void *) {
    while (!feeder_stop) {
        capture_feed_once();
        vTaskDelay(1);
    }
    for (;;) vTaskSuspend(nullptr);
}
extern "C" bool sk_audio_capture_active() { return capture_enabled; }
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
static esp_err_t capture_start(bool buffered) {
    if (!ready) return ESP_ERR_NOT_FOUND;
    sk_audio_capture_stop();
    if (!monitor_capture) sk_audio_output_finish(false);
    if (!sk_i2c_lock(0)) return ESP_ERR_TIMEOUT;
    bool ok = codec.inputSelect(AUDIO_INPUT_MIC) && codec.micGain(20);
    sk_i2c_unlock(0);
    if (!ok) return ESP_FAIL;
    auto *buffer = static_cast<CaptureBlock *>(solar_os_memory_alloc(
        capture_slots * sizeof(CaptureBlock), SOLAR_OS_MEMORY_INTERNAL_PREFERRED, "audio.capture"));
    if (!buffer) return ESP_ERR_NO_MEM;
    spool_head = spool_tail = spool_peak = 0;
    feeder_stack_free = 0;
    if (buffered) {
        spool = static_cast<CaptureBlock *>(solar_os_memory_alloc(
            spool_slots * sizeof(CaptureBlock), SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "audio.spool"));
        feeder_stack = static_cast<StackType_t *>(solar_os_memory_alloc(
            2048, SOLAR_OS_MEMORY_INTERNAL_PREFERRED, "audio.feeder"));
        if (!spool || !feeder_stack) {
            solar_os_memory_free(buffer);
            sk_audio_capture_stop();
            return ESP_ERR_NO_MEM;
        }
    }
    AudioNoInterrupts();
    captured = buffer;
    test_tone_started = false;
    capture_head = capture_tail = 0;
    capture_drops = capture_blocks = 0;
    capture_enabled = true;
    AudioInterrupts();
    if (buffered) {
        feeder_stop = false;
        feeder = xTaskCreateStatic(capture_feeder, "audio-capture", 2048 / sizeof(StackType_t),
                                  nullptr, 3, feeder_stack, &feeder_tcb);
        if (!feeder) { sk_audio_capture_stop(); return ESP_ERR_NO_MEM; }
    }
    return ESP_OK;
}
extern "C" esp_err_t sk_audio_capture_start() { return capture_start(false); }
extern "C" esp_err_t sk_audio_capture_start_buffered() { return capture_start(true); }
extern "C" esp_err_t sk_audio_capture_read(int16_t *mono, size_t capacity, size_t *frames) {
    *frames = 0;
    if (!captured || !capture_enabled) return ESP_ERR_INVALID_STATE;
    uint32_t started = millis();
    // Buffered recordings aggregate 4 KiB writes, rather than one SD operation
    // per 128-sample interrupt. No console lock is needed by the feeder.
    if (capacity < AUDIO_BLOCK_SAMPLES) return ESP_ERR_INVALID_ARG;
    const unsigned need = min(capacity / AUDIO_BLOCK_SAMPLES, size_t(spool_slots - 1));
    while (spool ? ((spool_head + spool_slots - spool_tail) % spool_slots < need)
                 : capture_head == capture_tail) {
        if (capture_drops) return ESP_FAIL;
        if (sk_audio_cancelled()) return ESP_ERR_TIMEOUT;
        if (millis() - started > 1000) return ESP_FAIL;
        vTaskDelay(1);
    }
    if (capture_test && !test_tone_started && capture_blocks >= 345) {
        tone_until = millis() + 1000;
        __DMB(); tone_on = true; test_tone_started = true;
    }
    if (capture_drops) return ESP_FAIL;
    if (spool) {
        while (spool_head != spool_tail && *frames + AUDIO_BLOCK_SAMPLES <= capacity) {
            const unsigned t = spool_tail;
            memcpy(mono + *frames, spool[t], sizeof(CaptureBlock));
            __DMB(); spool_tail = (t + 1) % spool_slots;
            *frames += AUDIO_BLOCK_SAMPLES;
        }
    } else {
        while (capture_head != capture_tail && *frames + AUDIO_BLOCK_SAMPLES <= capacity) {
            const unsigned t = capture_tail;
            memcpy(mono + *frames, captured[t], sizeof(CaptureBlock));
            __DMB(); capture_tail = (t + 1) % capture_slots;
            *frames += AUDIO_BLOCK_SAMPLES;
        }
    }
    return ESP_OK;
}
extern "C" uint32_t sk_audio_capture_stop() {
    AudioNoInterrupts();
    capture_enabled = false;
    if (capture_test) tone_on = false;
    AudioInterrupts();
    // Stop production first, then join the feeder before detaching either ring.
    // It never takes a lock held by the foreground, so this cannot deadlock.
    feeder_stop = true;
    if (feeder) {
        while (eTaskGetState(feeder) != eSuspended) vTaskDelay(1);
        feeder_stack_free = uxTaskGetStackHighWaterMark(feeder) * sizeof(StackType_t);
        vTaskDelete(feeder); feeder = nullptr;
    }
    auto *buffer = captured;
    captured = nullptr;
    capture_head = capture_tail = 0;
    solar_os_memory_free(buffer);
    solar_os_memory_free(spool); spool = nullptr;
    solar_os_memory_free(feeder_stack); feeder_stack = nullptr;
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
    if (file_worker_context()) {
#if SK_LCD_CONSOLE
        if (!sk_audio_owner_connected()) return true;
#endif
        return file_cancel && file_cancel(file_user);
    }
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
// Test/alarm tones are generated directly into AudioStream blocks, without a ring.
static esp_err_t configure_output(uint8_t volume) {
    if (!ready) return ESP_ERR_NOT_FOUND;
    sk_audio_output_finish(false);
    if (!sk_i2c_lock(0)) return ESP_ERR_TIMEOUT;
    const bool configured = codec.volume((volume == 255 ? 20 : volume) / 100.0f);
    sk_i2c_unlock(0);
    played = underruns = tone_blocks = 0;
    return configured ? ESP_OK : ESP_FAIL;
}
extern "C" esp_err_t sk_audio_output_start(uint8_t volume) {
    const esp_err_t err = configure_output(volume);
    if (err != ESP_OK) return err;
    auto *buffer = static_cast<PlaybackBlock *>(solar_os_memory_alloc(
        slots * sizeof(PlaybackBlock), SOLAR_OS_MEMORY_INTERNAL_PREFERRED, "audio.playback"));
    if (!buffer) return ESP_ERR_NO_MEM;
    AudioNoInterrupts();
    pcm = buffer;
    AudioInterrupts();
    if (file_worker_context()) {
        file_head=file_tail=file_peak=file_partial=0;
        file_eof=file_primed=file_feed_stop=false;
        file_feed_stack_free=0;
        file_pcm=static_cast<PlaybackBlock *>(solar_os_memory_alloc(
            file_slots*sizeof(PlaybackBlock),SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"player.pcm"));
        file_feed_stack=static_cast<StackType_t *>(solar_os_memory_alloc(
            2048,SOLAR_OS_MEMORY_INTERNAL_PREFERRED,"player.feeder"));
        if (file_pcm && file_feed_stack)
            file_feed_task=xTaskCreateStatic(file_feed,"player-pcm",2048/sizeof(StackType_t),
                                            nullptr,3,file_feed_stack,&file_feed_tcb);
        if (!file_feed_task) { sk_audio_output_finish(false); return ESP_ERR_NO_MEM; }
    }
    return ESP_OK;
}
extern "C" esp_err_t sk_audio_output_write(const int16_t *data, size_t frames) {
    if (!pcm) return ESP_ERR_INVALID_STATE;
    if (file_pcm) return file_output_write(data,frames);
    while (frames) {
        if (!output_wait_ready(nullptr)) return ESP_ERR_TIMEOUT;
        uint32_t started = millis();
        while ((head + 1) % slots == tail) {
            if (!output_wait_ready(&started)) return ESP_ERR_TIMEOUT;
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
    if (!pcm) return ESP_ERR_INVALID_STATE;
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
    if (file_pcm && drain) {
        if (file_partial) {
            memset(file_pcm[file_head]+file_partial*2,0,(AUDIO_BLOCK_SAMPLES-file_partial)*4);
            __DMB(); file_head=(file_head+1)%file_slots; file_partial=0;
        }
        file_eof=true;
        uint32_t started=millis();
        while (file_head!=file_tail) {
            if (!output_wait_ready(&started) || sk_audio_cancelled()) {result=ESP_ERR_TIMEOUT;break;}
            if (millis()-started>1500) {result=ESP_FAIL;break;}
            vTaskDelay(1);
        }
    }
    file_feed_release();
    if (result!=ESP_OK) drain=false;
    consuming = false; // The producer is finished; an empty tail is no underrun.
    if (drain && partial) {
        memset(pcm[head] + partial * 2, 0, (AUDIO_BLOCK_SAMPLES - partial) * 4);
        __DMB(); head = (head + 1) % slots; partial = 0;
    }
    uint32_t started = millis();
    while (drain && head != tail) {
        if (!output_wait_ready(&started)) { result = ESP_ERR_TIMEOUT; break; }
        if (sk_audio_cancelled()) { result = ESP_ERR_TIMEOUT; break; }
        if (millis() - started > 1000) { result = ESP_FAIL; break; }
        vTaskDelay(1);
    }
    // Two I2S blocks can remain downstream of the source.
    if (drain && result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(10));
    AudioNoInterrupts();
    consuming = tone_on = output_paused = false;
    head = tail = partial = 0;
    auto *buffer = pcm;
    pcm = nullptr;
    AudioInterrupts();
    solar_os_memory_free(buffer);
    return result;
}
void sk_audio_player_tone(bool on) {
    if (!diagnostic_allowed()) { sk_console_print("Audio busy; stop the audio app first\r\n"); return; }
    if (!ready) { sk_console_print("Audio unavailable\r\n"); return; }
    if (!on) { sk_audio_output_finish(false); return; }
    if (configure_output(20) != ESP_OK) return;
    tone_until = millis() + 1000;
    __DMB();
    tone_on = on;
}
extern "C" void sk_audio_output_status() {
    sk_console_printf("Audio: SGTL5000=%s rate=44100 stereo blocks=%lu underruns=%lu\r\n",
        ready ? "ready" : "missing", (unsigned long)played, (unsigned long)underruns);
    sk_console_printf("Audio rings: playback=%u capture=%u bytes (internal RAM)\r\n",
        pcm ? unsigned(slots * sizeof(PlaybackBlock)) : 0,
        captured ? unsigned(capture_slots * sizeof(CaptureBlock)) : 0);
    sk_console_printf("File player: running=%u paused=%u stack_free=%lu bytes\r\n",
        unsigned(file_worker!=nullptr), unsigned(output_paused), (unsigned long)file_stack_free);
    sk_console_printf("Player buffer: allocated=%u peak=%u bytes; feeder stack free=%lu bytes\r\n",
        file_pcm?unsigned(file_slots*sizeof(PlaybackBlock)):0, unsigned(file_peak*sizeof(PlaybackBlock)),
        (unsigned long)file_feed_stack_free);
    sk_console_printf("SD capture buffer: allocated=%u peak=%u bytes; feeder stack free=%lu bytes\r\n",
        spool ? unsigned(spool_slots * sizeof(CaptureBlock)) : 0,
        unsigned(spool_peak * sizeof(CaptureBlock)), (unsigned long)feeder_stack_free);
    sk_console_printf("Capture: mic gain=20dB blocks=%lu overruns=%lu\r\n",
        (unsigned long)capture_blocks, (unsigned long)capture_drops);
}
extern "C" void solar_os_shell_cmd_audio(solar_os_context_t *ctx, int argc, char **argv) {
    if (argc == 1 || (argc == 2 && !strcmp(argv[1], "status"))) {
        sk_audio_output_status();
    } else if (argc == 3 && !strcmp(argv[1], "monitor")) {
        auto *io = solar_os_context_shell_io(ctx);
        bool busy = diagnostic_busy || capture_enabled;
#if SK_LCD_CONSOLE
        busy = busy || sk_console_recorder_busy();
#endif
        if (busy) { solar_os_shell_io_writeln(io, "Audio capture busy"); return; }
        char path[160];
        if (solar_os_shell_resolve_path(ctx, argv[2], path, sizeof(path)) != ESP_OK) return;
        diagnostic_busy = monitor_capture = true;
        solar_os_audio_wav_info_t info{};
        const esp_err_t err = solar_os_audio_record_wav(path, 4000, nullptr, &info);
        monitor_capture = diagnostic_busy = false;
        solar_os_shell_io_printf(io, "Audio monitor: %s, %lu bytes, %lu ms\n",
            esp_err_to_name(err), (unsigned long)info.data_bytes, (unsigned long)info.duration_ms);
    } else if (!diagnostic_allowed()) {
        solar_os_shell_io_writeln(solar_os_context_shell_io(ctx), "Audio busy; stop the audio app first");
    } else if (argc == 2 && !strcmp(argv[1], "tone")) {
        sk_audio_player_tone(true);
        solar_os_shell_io_writeln(solar_os_context_shell_io(ctx), ready ?
            "440 Hz test tone for one second, headphones at 20%" : "Audio shield missing");
    } else if (argc == 3 && !strcmp(argv[1], "mictest")) {
        char path[160];
        auto *io = solar_os_context_shell_io(ctx);
        if (solar_os_shell_resolve_path(ctx, argv[2], path, sizeof(path)) != ESP_OK) return;
        solar_os_shell_io_writeln(io, "Recording 4 seconds; a one-second tone follows one second of silence.");
        diagnostic_busy = true;
        esp_err_t err = configure_output(30);
        solar_os_audio_wav_info_t info{};
        if (err == ESP_OK) {
            capture_test = true;
            err = solar_os_audio_record_wav(path, 4000, nullptr, &info);
            capture_test = false;
        }
        diagnostic_busy = false;
        solar_os_shell_io_printf(io, "Mic test: %s, %lu bytes, %lu ms\n", esp_err_to_name(err),
            (unsigned long)info.data_bytes, (unsigned long)info.duration_ms);
        solar_os_shell_io_printf(io, "Mic test tone: started=%u blocks=%lu\n",
            unsigned(test_tone_started), (unsigned long)tone_blocks);
    } else if (argc == 2 && !strcmp(argv[1], "off")) {
        sk_audio_player_tone(false);
    } else {
        solar_os_shell_io_writeln(solar_os_context_shell_io(ctx), "usage: audio [status|tone|off|mictest new.wav|monitor new.wav]");
    }
}
#endif
