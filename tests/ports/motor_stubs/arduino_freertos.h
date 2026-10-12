#pragma once
#include "../wireless_stubs/arduino_freertos.h"
#define LOW 0
#define HIGH 1
#define OUTPUT 2
#define INPUT_PULLUP 3
extern uint32_t test_primask;
inline uint32_t __get_PRIMASK(){return test_primask;}
inline void __disable_irq(){test_primask=1;}
inline void __enable_irq(){test_primask=0;}
void digitalWrite(int,int);
