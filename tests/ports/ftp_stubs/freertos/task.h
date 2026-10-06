#pragma once
#include "FreeRTOS.h"
#define eSuspended 1
#ifdef __cplusplus
extern "C" {
#endif
int eTaskGetState(TaskHandle_t);
#ifdef __cplusplus
}
#endif
