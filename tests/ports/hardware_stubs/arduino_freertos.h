#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cassert>
#include "semphr.h"
#define DMAMEM
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define configASSERT(x) assert(x)
extern int test_modes[64],test_values[64],test_pin_calls[64];
inline void pinMode(unsigned pin,int mode){assert(pin<64);test_modes[pin]=mode;++test_pin_calls[pin];}
inline void digitalWrite(unsigned pin,int value){assert(pin<64);test_values[pin]=value;++test_pin_calls[pin];}
inline int digitalRead(unsigned pin){assert(pin<64);return test_values[pin];}
extern "C" size_t strlcpy(char *,const char *,size_t);
struct HardwareSerial {
    unsigned begins=0,ends=0,written=0;bool active=false;int room=16;
    void addMemoryForRead(void *,size_t){}
    void begin(uint32_t){++begins;active=true;}
    void end(){++ends;active=false;}
    int read(){return -1;}
    int available(){return 0;}
    int availableForWrite(){return room;}
    size_t write(const uint8_t *,size_t n){assert(active);written+=n;return n;}
};
using HardwareSerialIMXRT=HardwareSerial;
extern HardwareSerial Serial7,Serial8,Serial3;
inline int analogRead(unsigned pin){assert(pin<42);return 512;}
inline void analogWrite(unsigned pin,int value){assert(pin<42);test_values[pin]=value;}
inline void analogWriteFrequency(unsigned pin,int hz){assert(pin<42 && hz>0);}
inline uint32_t analogWriteResolution(uint32_t bits){return bits;}
inline uint32_t millis(){return 1234;}
inline uint32_t micros(){return 1234567;}
