#pragma once
#ifndef SK_USB_HOST_POWER
#define SK_USB_HOST_POWER 0
#endif
#if SK_USB_HOST_POWER
void sk_usb_host_power_begin();
void sk_usb_host_power_off();
#else
inline void sk_usb_host_power_begin() {}
inline void sk_usb_host_power_off() {}
#endif
