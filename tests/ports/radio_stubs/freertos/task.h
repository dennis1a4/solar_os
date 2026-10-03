#pragma once
#include "FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
void vTaskDelay(TickType_t);
void vTaskSuspend(TaskHandle_t);
#ifdef __cplusplus
}
#endif
