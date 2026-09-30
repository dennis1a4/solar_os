#if SK_SCOPE
#include <arduino_freertos.h>
#include <ADC.h>
#include <DMAChannel.h>
#include <new>
#include <string.h>
#include "solar_os_scope_capture.h"
#include "board.h"
#ifndef SK_SCOPE_ADC_PIN
#define SK_SCOPE_ADC_PIN -1
#endif
static_assert(SK_SCOPE_ADC_PIN < 0 || SK_SCOPE_ADC_PIN != SK_AMPLIFIER_SHUTDOWN_PIN, "Scope input conflicts with AmpEn");
/* Reserved OCRAM: DMA cannot write the app's PSRAM capture array. No ISR,
 * overwrite race or cache-line sharing: each transfer stops at 1024 samples. */
DMAMEM static uint16_t samples[SOLAR_SCOPE_SAMPLES] __attribute__((aligned(32)));
struct Capture {
    ADC adc;
    DMAChannel dma{false};
    bool armed = false;
    uint32_t rate = 0, began = 0;
};
static Capture *capture;
extern "C" int solar_scope_capture_pin(void) { return SK_SCOPE_ADC_PIN; }
extern "C" bool solar_scope_capture_open(void) {
    if (SK_SCOPE_ADC_PIN < 0 || capture) return false;
    capture = new (std::nothrow) Capture;
    if (!capture) return false;
    capture->dma.begin(true);
    if (!capture->dma.TCD ||
        !capture->adc.adc0->checkPin(SK_SCOPE_ADC_PIN)) {
        delete capture; capture = nullptr; return false;
    }
    pinMode(SK_SCOPE_ADC_PIN, INPUT);
    capture->adc.adc0->setResolution(12);
    capture->adc.adc0->setAveraging(0);
    capture->adc.adc0->setConversionSpeed(ADC_CONVERSION_SPEED::HIGH_SPEED);
    capture->adc.adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);
    return true;
}
extern "C" void solar_scope_capture_cancel(void) {
    if (!capture) return;
    capture->dma.disable();
    capture->adc.adc0->disableDMA();
    if (capture->armed) capture->adc.adc0->stopTimer();
    capture->adc.adc0->stopContinuous();
    capture->armed = false;
}
extern "C" bool solar_scope_capture_arm(uint32_t rate) {
    if (!capture || rate < 1000 || rate > 100000) return false;
    solar_scope_capture_cancel();
    auto *adc = capture->adc.adc0;
    // Configure the channel, discard the initial software-triggered conversion.
    if (!adc->startSingleRead(SK_SCOPE_ADC_PIN)) return false;
    uint32_t began = micros();
    while (!adc->isComplete()) {
        if ((uint32_t)(micros() - began) > 2000) return false;
    }
    (void)adc->readSingle();
    arm_dcache_flush_delete(samples, sizeof(samples));
    auto &dma = capture->dma;
    dma.clearComplete();
    dma.source(ADC1_R0);
    dma.destinationBuffer(samples, sizeof(samples));
    dma.transferSize(2);
    dma.disableOnCompletion();
    dma.triggerAtHardwareEvent(DMAMUX_SOURCE_ADC1);
    adc->enableDMA();
    dma.enable();
    adc->startTimer(rate);
    capture->armed = true;
    capture->rate = adc->getTimerFrequency();
    capture->began = millis();
    if (!capture->rate) { solar_scope_capture_cancel(); return false; }
    return true;
}
extern "C" int solar_scope_capture_take(uint16_t *out, size_t count, uint32_t *rate) {
    if (!capture || !capture->armed || !out || !rate || count != SOLAR_SCOPE_SAMPLES) return -1;
    if (!capture->dma.complete()) {
        if ((uint32_t)(millis() - capture->began) > 1000 + SOLAR_SCOPE_SAMPLES * 1000 / capture->rate) {
            solar_scope_capture_cancel(); return -1;
        }
        return 0;
    }
    uint32_t actual = capture->rate;
    solar_scope_capture_cancel();
    arm_dcache_delete(samples, sizeof(samples));
    memcpy(out, samples, sizeof(samples));
    *rate = actual;
    return 1;
}
extern "C" void solar_scope_capture_close(void) {
    solar_scope_capture_cancel();
    delete capture; capture = nullptr;
}
#endif
