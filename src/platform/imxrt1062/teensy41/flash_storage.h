#pragma once
#if SK_QSPI_FLASH
#include "littlefs/lfs.h"
void sk_flash_begin();
bool sk_flash_mounted();
lfs_t *sk_flash_fs();
void sk_flash_acquire();
void sk_flash_release();
#endif
