#pragma once
using StaticSemaphore_t=int;
using SemaphoreHandle_t=void *;
inline void *xSemaphoreCreateMutexStatic(StaticSemaphore_t *p){return p;}
inline void xSemaphoreTake(void *,unsigned){}
inline void xSemaphoreGive(void *){}
