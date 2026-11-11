#pragma once
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include "freertos/FreeRTOS.h"
#define DMAMEM
using StackType_t = uint32_t;
struct StaticTask_t { unsigned unused; };
using TaskHandle_t = StaticTask_t *;
extern uint32_t motion_test_now;
extern unsigned motion_test_critical;
inline uint32_t millis() { return motion_test_now; }
inline void vTaskDelay(unsigned ticks) { motion_test_now += ticks; }
inline TaskHandle_t xTaskCreateStatic(void (*)(void *), const char *, unsigned,
    void *, unsigned, StackType_t *, StaticTask_t *storage) { return storage; }
#define taskENTER_CRITICAL() (++motion_test_critical)
#define taskEXIT_CRITICAL() (--motion_test_critical)
