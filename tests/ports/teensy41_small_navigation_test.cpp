#include "small_navigation.h"
#include <cassert>
#include <cstdio>
int main() {
    const unsigned expected[]={1,2,4,8,64,128,32,16};
    for(unsigned i=0;i<8;++i) assert(sk_nes_buttons(1U<<i)==expected[i]);
    const unsigned onboard_expected[]={4,8,1,2,64,16,32,128};
    for(unsigned i=0;i<8;++i) assert(sk_onboard_nes_buttons(1U<<i)==onboard_expected[i]);
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
    n.update(32,1100,false); n.update(32,1120,false); assert(n.page==6);
    n.page=3; // Mesh page index is retained when sensor pages are added.
    n.update(128,1130,false); n.update(128,1150,false); assert(n.slot==2);
    n.update(4,1160,false); n.update(4,1180,false); assert(n.page==0);
    n.update(16,1200,true); n.update(16,1220,true); assert(n.page==0);
    n.update(16,1240,false); assert(n.page==0); // no phantom press after game
    n.update(0,1250,false); n.update(0,1270,false);
    n.update(16,1280,false); n.update(16,1300,false); assert(n.page==1);
    SmallNavigation wrap;
    wrap.update(16,0xfffffff0,false);
    wrap.update(16,4,false); assert(wrap.page==1); // uptime rollover
    SmallNavigation pages;
    unsigned page_now=0;
    for(unsigned i=0;i<10;i++) {
        pages.update(0,page_now,false); pages.update(0,page_now+20,false);
        pages.update(16,page_now+30,false); pages.update(16,page_now+50,false);
        assert(pages.page==(i+1)%7);
        page_now+=60;
    }
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
    NesInputs input;
    input.update(4,2,100);input.update(4,2,120);
    assert(input.held[0]==1 && input.held[1]==2 && input.combined()==3);
    // External bounce must not delay or release the on-board state.
    input.update(0,0,130);input.update(0,2,140);input.update(0,0,150);
    assert(input.held[0]==0 && input.held[1]==2);
    input.update(0,0,170);assert(input.combined()==0);
    // Independent holds: releasing one source cannot release the other's A.
    input.update(4,1,180);input.update(4,1,200);
    input.update(0,1,210);input.update(0,1,230);
    assert(input.held[0]==0 && input.held[1]==1 && input.combined()==1);
    // A grounded stream is rejected without suppressing the other source.
    input.update(255,2,240);input.update(255,2,260);
    assert(input.held[0]==0 && input.held[1]==2);
    // Uptime wrap and independent full button mapping.
    NesInputs rollover;rollover.update(4,2,0xfffffff8);rollover.update(4,2,12);
    assert(rollover.held[0]==1 && rollover.held[1]==2);
    for(unsigned a=0;a<256;a++)for(unsigned b=0;b<256;b++) {
        NesInputs pads;pads.update(a,b,10);pads.update(a,b,30);
        assert(pads.held[0]==(a==255?0:sk_onboard_nes_buttons(a)));
        assert(pads.held[1]==(b==255?0:sk_nes_buttons(b)));
        assert(pads.combined()==(pads.held[0]|pads.held[1]));
    }
    SmallNavigation direct;
    direct.update_debounced(16,false);assert(direct.page==1);
    direct.update_debounced(0,false);direct.update_debounced(32,false);assert(direct.page==0);
    direct.update_debounced(16,true);direct.update_debounced(16,false);assert(direct.page==0);
    puts("PASS: NES mapping, debounce, hold, page/slot wrap, home, ownership, timer wrap");
}
