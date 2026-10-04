#pragma once
#include <cstdint>
#include <cstring>
#include <cassert>
#define DMAMEM
#define configASSERT(x) assert(x)
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define pdMS_TO_TICKS(x) (x)
using StackType_t=uint32_t;
using StaticTask_t=int;
extern uint32_t test_now;
inline uint32_t millis(){return test_now;}
inline void vTaskDelay(unsigned){}
inline void *xTaskCreateStatic(void (*)(void *),const char *,unsigned,void *,unsigned,StackType_t *,StaticTask_t *p){return p;}
