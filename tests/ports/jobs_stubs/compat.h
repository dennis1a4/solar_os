#pragma once
#include <stddef.h>
#include <stdint.h>
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t StackType_t;
typedef struct {int unused;} StaticTask_t;
#define pdPASS 1
#define tskNO_AFFINITY 0
#define SOLAR_OS_JOBS_MAX 4
#ifdef __cplusplus
extern "C" {
#endif
TaskHandle_t xTaskGetCurrentTaskHandle(void);
#ifndef __cplusplus
size_t strlcpy(char *,const char *,size_t);
#endif
#ifdef __cplusplus
}
#endif
