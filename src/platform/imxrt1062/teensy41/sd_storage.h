#pragma once
#if SK_SD_RECOVERY
#include <SD.h>
// Caller holds StorageLock, except begin/poll/status which acquire it themselves.
FsVolume *sk_sd_volume();
bool sk_sd_media_ready();
bool sk_sd_acquire();
void sk_sd_release();
void sk_sd_poll();
#endif
