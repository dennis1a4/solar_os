#pragma once
#include "freertos/semphr.h"
static inline SemaphoreHandle_t xSemaphoreCreateMutex(){static StaticSemaphore_t m;return &m;}
