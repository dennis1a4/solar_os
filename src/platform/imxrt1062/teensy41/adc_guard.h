#pragma once
#include <stdint.h>
// Serialize Arduino ADC conversions across the DMM and Python tasks. Neither
// changes the shared Arduino default resolution (10 bits).
bool sk_adc_lock(uint32_t timeout_ms);
void sk_adc_unlock();
