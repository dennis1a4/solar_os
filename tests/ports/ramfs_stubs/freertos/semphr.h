#pragma once
#include <stdlib.h>
#include <assert.h>
typedef int *SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffffU
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return calloc(1,sizeof(int)); }
static inline int xSemaphoreTake(SemaphoreHandle_t s,unsigned timeout) { (void)timeout; assert(s && !*s); *s=1; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t s) { assert(s && *s); *s=0; return 1; }
static inline void vSemaphoreDelete(SemaphoreHandle_t s) { assert(s && !*s);free(s); }
