#pragma once
#include <stdint.h>
// NES serial order converted to the Game Boy button mask (A,B,Select,Start,
// Right,Left,Up,Down). No heap, hardware, or RTOS dependencies.
static inline uint8_t sk_nes_buttons(uint8_t serial) {
    return (serial&15) | ((serial&128)>>3) | ((serial&64)>>1) |
        ((serial&16)<<2) | ((serial&32)<<2);
}
struct SmallNavigation {
    unsigned page=0, slot=0;
    uint8_t candidate=0, held=0;
    uint32_t changed=0;
    bool blocked=false;
    uint8_t update(uint8_t buttons,uint32_t now,bool owned) {
        if(buttons!=candidate) { candidate=buttons; changed=now; }
        if(owned!=blocked) { blocked=owned; held=buttons; }
        if(uint32_t(now-changed)<20) return 0;
        const uint8_t pressed=buttons & ~held;
        held=buttons;
        if(owned) return 0;
        if(pressed&4) page=0;
        else if((pressed&48)==16) page=(page+1)%5;
        else if((pressed&48)==32) page=(page+4)%5;
        if(page==3) {
            if((pressed&192)==64) slot=(slot+1)%3; // Up increases the slot number.
            else if((pressed&192)==128) slot=(slot+2)%3;
        }
        return pressed;
    }
};
