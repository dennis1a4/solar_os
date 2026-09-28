#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
extern uint32_t fake_millis;
inline uint32_t millis() { return fake_millis; }
inline void vTaskDelay(unsigned n) { fake_millis+=n; }
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
extern time_t fake_epoch;
struct TestClock { time_t get() { return fake_epoch; } };
extern TestClock Teensy3Clock;
