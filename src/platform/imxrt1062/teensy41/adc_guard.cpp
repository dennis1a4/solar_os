#if SK_HW_RESOURCES
#include <arduino_freertos.h>
#include <semphr.h>
#include "adc_guard.h"
static StaticSemaphore_t storage;
static SemaphoreHandle_t mutex;
bool sk_adc_lock(uint32_t timeout_ms) {
    taskENTER_CRITICAL();
    if(!mutex)mutex=xSemaphoreCreateMutexStatic(&storage);
    taskEXIT_CRITICAL();
    return mutex && xSemaphoreTake(mutex,pdMS_TO_TICKS(timeout_ms))==pdTRUE;
}
void sk_adc_unlock() { xSemaphoreGive(mutex); }
#endif
