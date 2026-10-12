#pragma once
#include <stdint.h>

namespace nes_waveform {
// 32 LSB-first slots, each at least 6 us. Latch is high for slots 0..1.
// Sample A in slot 3, then B..Right in slots 5,7,...17. Clock rises in
// slots 4,6,...18, leaving ample settling time before each selected sample.
// Slots 19..31 leave both outputs low; no continuous clock between scans.
constexpr uint32_t latch=3;
constexpr uint32_t clock=0x55550;
constexpr unsigned slots=32;
constexpr uint32_t interval_ms=10;
inline uint8_t decode(uint32_t samples) {
    uint8_t buttons=0;
    for(unsigned bit=0;bit<8;++bit)
        if(!(samples & (1UL<<(3+2*bit)))) buttons|=1U<<bit;
    return buttons;
}
inline unsigned divider(uint32_t hz) {
    // Timer toggles twice per slot. Round up so timing is never faster.
    return unsigned((uint64_t(hz)*3+999999)/1000000);
}
}
