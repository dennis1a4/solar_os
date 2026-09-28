#if SK_SYNTH
#include <arduino_freertos.h>
#include <semphr.h>
#include "audio_output.h"
extern "C" {
#include "solar_os_synth.h"
#include "solar_os_task.h"
}

// Single foreground owner; worker state is permanent so a delayed shutdown
// cannot access freed app memory. PCM scratch is internal OCRAM, not the heap.
static StaticSemaphore_t mutex_storage, started_storage;
static SemaphoreHandle_t mutex, started;
static TaskHandle_t worker;
static volatile bool stopping, done;
static solar_os_synth_status_t status;
static solar_os_synth_render_cb_t render;
static void *render_user;
DMAMEM static int16_t samples[SOLAR_OS_SYNTH_BLOCK_FRAMES_MAX * 2];
static void init() {
    if (!mutex) {
        mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
        started = xSemaphoreCreateBinaryStatic(&started_storage);
    }
}
static void lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
static void unlock() { xSemaphoreGive(mutex); }

static void run(void *) {
    esp_err_t err = sk_audio_output_start(20);
    lock();
    status.starting = false;
    status.running = err == ESP_OK;
    status.last_error = err;
    status.sample_rate = err == ESP_OK ? 44100 : 0;
    const size_t frames = status.block_frames;
    unlock();
    xSemaphoreGive(started);
    while (err == ESP_OK && !stopping) {
        uint32_t before = micros();
        render(samples, frames, 44100, render_user);
        uint32_t elapsed = micros() - before;
        lock();
        ++status.rendered_blocks;
        status.rendered_frames += frames;
        status.max_render_us = max(status.max_render_us, elapsed);
        if (elapsed > frames * 1000000ULL / 44100) ++status.render_deadline_misses;
        unlock();
        err = sk_audio_synth_write(samples, frames, &stopping);
    }
    // A requested stop cancels a blocked write normally, without an error.
    if (stopping) err = ESP_OK;
    sk_audio_output_finish(false);
    lock();
    status.running = status.starting = false;
    status.last_error = err;
    if (err != ESP_OK) ++status.write_errors;
    done = true;
    unlock();
    solar_os_task_delete_internal(nullptr);
}
extern "C" esp_err_t solar_os_synth_start(const solar_os_synth_config_t *cfg) {
    if (!cfg || !cfg->owner || !*cfg->owner ||
        strlen(cfg->owner) >= SOLAR_OS_SYNTH_OWNER_MAX || !cfg->render ||
        cfg->block_frames < SOLAR_OS_SYNTH_BLOCK_FRAMES_MIN ||
        cfg->block_frames > SOLAR_OS_SYNTH_BLOCK_FRAMES_MAX) return ESP_ERR_INVALID_ARG;
    if (cfg->playback_stream && *cfg->playback_stream) return ESP_ERR_NOT_SUPPORTED;
    init();
    if (worker) {
        if (!done) return ESP_ERR_INVALID_STATE;
        if (!solar_os_task_wait_done(worker, &done, SOLAR_OS_TASK_STOP_WAIT_MS)) return ESP_ERR_TIMEOUT;
        worker = nullptr;
    }
    while (xSemaphoreTake(started, 0) == pdTRUE) {}
    lock();
    status = {};
    strlcpy(status.owner, cfg->owner, sizeof(status.owner));
    strlcpy(status.playback_stream, "teensy.sgtl5000", sizeof(status.playback_stream));
    status.block_frames = cfg->block_frames;
    status.starting = true;
    render = cfg->render; render_user = cfg->user;
    stopping = done = false;
    unlock();
    if (solar_os_task_create_pinned_internal(run, "synth", 8192, nullptr, 1,
            &worker, 0, SOLAR_OS_TASK_ROLE_SYSTEM) != pdPASS) {
        lock(); status.starting = false; status.last_error = ESP_ERR_NO_MEM; unlock();
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(started, pdMS_TO_TICKS(2000)) != pdTRUE) {
        solar_os_synth_stop(cfg->owner);
        return ESP_ERR_TIMEOUT;
    }
    lock(); esp_err_t err = status.last_error; unlock();
    return err;
}
extern "C" esp_err_t solar_os_synth_stop(const char *owner) {
    init();
    if (!worker) return ESP_OK;
    lock(); bool allowed = !owner || !strcmp(owner, status.owner); unlock();
    if (!allowed) return ESP_ERR_INVALID_STATE;
    stopping = true;
    if (!solar_os_task_wait_done(worker, &done, SOLAR_OS_TASK_STOP_WAIT_MS)) return ESP_ERR_TIMEOUT;
    worker = nullptr;
    return ESP_OK;
}
extern "C" void solar_os_synth_get_status(solar_os_synth_status_t *out) {
    if (!out) return;
    init(); lock(); *out = status; unlock();
}
#endif
