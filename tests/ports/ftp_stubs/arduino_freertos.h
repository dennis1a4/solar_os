#pragma once
#include <stdint.h>
#include <stddef.h>
extern uint32_t fake_millis;
inline uint32_t millis() {return fake_millis;}
inline void vTaskDelay(unsigned ticks) {fake_millis+=ticks;}
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
