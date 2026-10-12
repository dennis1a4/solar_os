#pragma once
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <initializer_list>
#define DMAMEM
#define configASSERT assert
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY UINT32_MAX
#define INPUT 0
#define INPUT_PULLDOWN 1
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
typedef uint32_t StackType_t;
typedef int StaticTask_t;
extern uint32_t test_clock;
inline uint32_t millis(){return test_clock;}
inline void vTaskDelay(unsigned ms){test_clock+=ms;}
inline void delayMicroseconds(unsigned){ }
inline void *xTaskCreateStatic(void(*)(void*),const char*,unsigned,void*,unsigned,StackType_t*,StaticTask_t*){return (void*)1;}
int digitalRead(int);
void pinMode(int,int);

typedef void *TaskHandle_t;
inline void xTaskNotifyGive(TaskHandle_t){}
inline unsigned ulTaskNotifyTake(int,unsigned){return 0;}

#define FALLING 2
inline void attachInterrupt(int,void(*)(),int){}
inline void detachInterrupt(int){}
