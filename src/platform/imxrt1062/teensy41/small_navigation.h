#pragma once
#include <stdint.h>
constexpr unsigned sk_small_page_count=7;
// NES serial order converted to the Game Boy button mask (A,B,Select,Start,
// Right,Left,Up,Down). No heap, hardware, or RTOS dependencies.
static inline uint8_t sk_nes_buttons(uint8_t serial) {
    return (serial&15) | ((serial&128)>>3) | ((serial&64)>>1) |
        ((serial&16)<<2) | ((serial&32)<<2);
}
// Motherboard NES_Controller.kicad_sch, U12 Q7: D7 through D0 are
// Select, Start, A, B, Up, Right, Left, Down. External NES keeps its own order.
static inline uint8_t sk_onboard_nes_buttons(uint8_t serial) {
    return ((serial&3)<<2) | ((serial&12)>>2) | ((serial&16)<<2) |
        ((serial&32)>>1) | ((serial&64)>>1) | (serial&128);
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
        return update_debounced(buttons,owned);
    }
    // Physical sources are debounced independently before navigation merges them.
    uint8_t update_debounced(uint8_t buttons,bool owned) {
        if(owned!=blocked) { blocked=owned; held=buttons; }
        const uint8_t pressed=buttons & ~held;
        held=buttons;
        if(owned) return 0;
        if(pressed&4) page=0;
        else if((pressed&48)==16) page=(page+1)%sk_small_page_count;
        else if((pressed&48)==32) page=(page+sk_small_page_count-1)%sk_small_page_count;
        if(page==3) {
            if((pressed&192)==64) slot=(slot+1)%3; // Up increases the slot number.
            else if((pressed&192)==128) slot=(slot+2)%3;
        }
        return pressed;
    }
};

struct NesInputs {
    uint8_t candidate[2]{}, held[2]{};
    uint32_t changed[2]{};
    void update(uint8_t onboard,uint8_t external,uint32_t now) {
        const uint8_t raw[]={onboard,external};
        for(unsigned i=0;i<2;++i) {
            // Reject a grounded data line separately; never discard the other pad.
            const uint8_t buttons=raw[i]==255?0:
                (i==0?sk_onboard_nes_buttons(raw[i]):sk_nes_buttons(raw[i]));
            if(candidate[i]!=buttons) {candidate[i]=buttons;changed[i]=now;}
            if(uint32_t(now-changed[i])>=20)held[i]=buttons;
        }
    }
    uint8_t combined() const {return held[0]|held[1];}
};
