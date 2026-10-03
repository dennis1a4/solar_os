#include "solar_os_memory.h"
#include <assert.h>
#include <stdlib.h>
unsigned settings_host_allocations;
bool settings_host_fail_alloc;
void *solar_os_memory_calloc(size_t n,size_t size,solar_os_memory_class_t kind,const char *tag) {
    (void)tag;
    assert(kind==SOLAR_OS_MEMORY_EXTERNAL_SYSTEM);
    if(settings_host_fail_alloc)return NULL;
    void *p=calloc(n,size);
    if(p)++settings_host_allocations;
    return p;
}
void solar_os_memory_free(void *p) {
    if(p){assert(settings_host_allocations);--settings_host_allocations;free(p);}
}
