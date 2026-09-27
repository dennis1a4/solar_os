#pragma once
#include "littlefs/lfs.h"
#ifdef __cplusplus
extern "C" {
#endif
int sk_flash_scan_blank(const struct lfs_config *config, void (*progress)(void));
int sk_flash_initialize_blank(lfs_t *fs, const struct lfs_config *config, void (*progress)(void));
#ifdef __cplusplus
}
#endif
