#pragma once
#include <cassert>
typedef bool StaticSemaphore_t;
typedef bool *SemaphoreHandle_t;
inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *s){*s=false;return s;}
inline int xSemaphoreTake(SemaphoreHandle_t s,unsigned wait){if(*s){assert(!wait);return 0;}*s=true;return 1;}
inline void xSemaphoreGive(SemaphoreHandle_t s){assert(*s);*s=false;}
