#pragma once
#if SK_USB_STORAGE
#include <USBHost_t36.h>

// All functions except begin require StorageLock. The USB ISR only invalidates
// media; filesystem teardown and discovery happen from the polling task.
void sk_usb_storage_begin();
void sk_usb_storage_poll();
bool sk_usb_storage_mounted();
FsVolume *sk_usb_storage_volume();
void sk_usb_storage_acquire();
void sk_usb_storage_release();
#endif
