#pragma once
#include <smalloc_i.h>

// The pinned allocator records the actual rounded payload size. We add both headers.
// Call only under the memory mutex. Every allocation in these two pools passes
// through these helpers, so observing free space does not scan megabytes of RAM.
static inline size_t sk_pool_charge(const void *ptr) {
    return USER_TO_HEADER(ptr)->rsz + 2 * HEADER_SZ;
}
static inline void *sk_pool_alloc(smalloc_pool *pool,size_t size,size_t &used) {
    void *ptr=sm_malloc_pool(pool,size);
    if(ptr) used+=sk_pool_charge(ptr);
    return ptr;
}
static inline void sk_pool_free(smalloc_pool *pool,void *ptr,size_t &used) {
    if(!ptr) return;
    used-=sk_pool_charge(ptr);
    sm_free_pool(pool,ptr);
}
