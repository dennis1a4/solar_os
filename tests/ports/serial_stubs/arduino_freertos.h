#pragma once
#include_next <arduino_freertos.h>
typedef uint32_t StackType_t;
typedef int StaticTask_t;
inline void *xTaskCreateStatic(void (*)(void *),const char *,unsigned,void *,unsigned,StackType_t *,StaticTask_t *t){return t;}
inline void vTaskDelay(unsigned){}
