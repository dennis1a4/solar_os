#pragma once
#include <cstddef>
struct smalloc_pool {void *pool;size_t pool_size;};
extern smalloc_pool extmem_smalloc_pool;
void *sm_malloc_pool(smalloc_pool *,size_t);
void sm_free_pool(smalloc_pool *,void *);
int sm_malloc_stats_pool(smalloc_pool *,size_t *,size_t *,size_t *,int *);

int sm_set_pool(smalloc_pool *,void *,size_t,int,void *);
