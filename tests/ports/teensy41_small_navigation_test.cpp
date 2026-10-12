#include "small_navigation.h"
#include <cassert>
#include <cstdio>
int main() {
    const unsigned expected[]={1,2,4,8,64,128,32,16};
    for(unsigned i=0;i<8;++i) assert(sk_nes_buttons(1U<<i)==expected[i]);
    SmallNavigation n;
    assert(n.update(16,0,false)==0);
    assert(n.update(0,5,false)==0); // bounce
    assert(n.update(16,10,false)==0);
    assert(n.update(16,29,false)==0 && n.page==0);
    assert(n.update(16,30,false)==16 && n.page==1);
    assert(n.update(16,1000,false)==0 && n.page==1); // held: no repeat
    n.update(0,1010,false); n.update(0,1030,false);
    n.update(32,1040,false); n.update(32,1060,false); assert(n.page==0);
    n.update(0,1070,false); n.update(0,1090,false);
    n.update(32,1100,false); n.update(32,1120,false); assert(n.page==3);
    n.update(128,1130,false); n.update(128,1150,false); assert(n.slot==2);
    n.update(4,1160,false); n.update(4,1180,false); assert(n.page==0);
    n.update(16,1200,true); n.update(16,1220,true); assert(n.page==0);
    n.update(16,1240,false); assert(n.page==0); // no phantom press after game
    n.update(0,1250,false); n.update(0,1270,false);
    n.update(16,1280,false); n.update(16,1300,false); assert(n.page==1);
    SmallNavigation wrap;
    wrap.update(16,0xfffffff0,false);
    wrap.update(16,4,false); assert(wrap.page==1); // uptime rollover
    SmallNavigation slots;
    slots.page=3;
    unsigned now=0;
    auto press=[&](uint8_t button) {
        slots.update(0,now,false); slots.update(0,now+20,false);
        slots.update(button,now+30,false); slots.update(button,now+50,false);
        now+=60;
    };
    press(64); assert(slots.slot==1);
    press(64); assert(slots.slot==2);
    press(64); assert(slots.slot==0);
    press(128); assert(slots.slot==2);
    press(128); assert(slots.slot==1);
    press(128); assert(slots.slot==0);
    puts("PASS: NES mapping, debounce, hold, page/slot wrap, home, ownership, timer wrap");
}
