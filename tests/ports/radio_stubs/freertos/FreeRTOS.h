#pragma once
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
typedef uint32_t TickType_t;
#define pdPASS 1
#define pdFAIL 0
#ifdef __cplusplus
extern "C" {
#endif
void radio_enter(void);
void radio_exit(void);
#define taskENTER_CRITICAL radio_enter
#define taskEXIT_CRITICAL radio_exit
#ifdef __cplusplus
}
#endif
