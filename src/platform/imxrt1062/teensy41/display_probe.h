#pragma once
#include <stdint.h>

// Probe readable cursor registers rather than trusting driver configuration.
// This runs once at boot, before the console draws anything.
template<class Display> bool sk_primary_display_present(Display &display) {
    if(display.errorCode()!=0) return false;
    display.setFontDefault();
    bool present=true;
    for(unsigned i=0;i<2;++i) {
        const int16_t px=i?173:37, py=i?91:53;
        int16_t x=-1,y=-1;
        display.setCursor(px,py);
        display.getCursor(x,y);
        if(x!=px || y!=py) present=false;
    }
    display.setCursor(0,0);
    return present;
}
