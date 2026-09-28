#pragma once
#include "solar_os_memory.h"
#define MALLOC_CAP_SPIRAM 1U
#define MALLOC_CAP_8BIT 2U
static inline void *heap_caps_malloc(size_t n,unsigned caps) {
    return solar_os_memory_alloc(n,(caps&MALLOC_CAP_SPIRAM)?SOLAR_OS_MEMORY_EXTERNAL_REQUIRED:SOLAR_OS_MEMORY_EXTERNAL_PREFERRED,"image.decoder");
}
static inline void heap_caps_free(void *p) { solar_os_memory_free(p); }
extern uint8_t external_psram_size;
static inline size_t heap_caps_get_total_size(unsigned caps) { (void)caps; return (size_t)external_psram_size*1024U*1024U; }
