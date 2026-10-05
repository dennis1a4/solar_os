#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// Allocatable heap regions only; excludes static data/code and reserved stacks.
void sk_memory_internal_regions(size_t *dtcm_free, size_t *dtcm_total,
                                size_t *ocram_free, size_t *ocram_total);
#ifdef __cplusplus
}
#endif
