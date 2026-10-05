#include "keyboard_input.h"
#include "keyboard_numlock.h"
#include <cassert>
#include <cstdio>
#include <string>
using Input=TeensyKeyboardInput;
static std::string read(Input &k,uint32_t now) {
    std::string result;
    for(int ch;(ch=k.read(now))>=0;) { result+=char(ch);assert(result.size()<1024); }
    return result;
}
static std::string encode(uint8_t key,uint8_t mods=0,uint8_t leds=0) {
    auto s=Input::encode(key,mods,leds);return std::string(reinterpret_cast<char *>(s.bytes),s.size);
}
int main() {
    KeyboardNumLockStartup startup;
    startup.request(true);assert(!startup.take_request());
    startup.connect(true);assert(!startup.take_request()); // SET_IDLE in flight
    startup.initialized();assert(startup.take_request());assert(!startup.take_request());
    // Polling must not turn the lock back on after a physical key toggle.
    for(unsigned i=0;i<100;++i)assert(!startup.take_request());
    startup.disconnect();assert(!startup.connected() && !startup.take_request());
    startup.connect(true);assert(!startup.take_request());
    startup.initialized();assert(startup.take_request()); // same logical LED bit still needs sending
    startup.disconnect();startup.initialized();assert(!startup.take_request());
    startup.connect(false);startup.initialized();assert(!startup.take_request());
    startup.request(true);assert(startup.take_request());
    startup.connect(true);startup.request(false);startup.initialized();assert(!startup.take_request());
    Input k;
    k.press(4,0);assert(read(k,0)=="a");assert(read(k,399).empty());assert(read(k,400)=="a");
    assert(read(k,432).empty());assert(read(k,433)=="a");k.release(4);assert(read(k,1000).empty());
    k.press(4,1000);assert(read(k,1000)=="a");assert(read(k,100000)=="a");assert(read(k,100000).empty());
    k.release(4);
    // Newest key wins; releasing it does not revive an older held key.
    k.press(4,0);k.press(5,100);assert(read(k,100)=="ab");assert(read(k,500)=="b");
    k.release(4);assert(read(k,533)=="b");k.release(5);assert(read(k,600).empty());
    k.press(4,1000);read(k,1000);k.press(5,1010);read(k,1010);k.release(5);
    assert(read(k,2000).empty());k.press(4,2001);assert(read(k,3000).empty());k.release(4);
    // Shift changes during the hold affect the next repeat; Ctrl suppresses it.
    k.press(4,0);assert(read(k,0)=="a");k.modifiers=2;assert(read(k,400)=="A");
    k.modifiers=0;assert(read(k,433)=="a");k.modifiers=1;assert(read(k,466).empty());
    k.modifiers=0;assert(read(k,499)=="a");k.release(4);
    // The original press remains valid after a quick tap, but no repeat survives.
    k.press(4,0);k.release(4);assert(read(k,1000)=="a");assert(read(k,2000).empty());
    // Never cut a generated ANSI key in half on normal release.
    k.press(80,0);assert(read(k,0)=="\033[D");assert(k.read(400)==27);k.release(80);
    assert(read(k,400)=="[D");assert(read(k,500).empty());
    // Disconnect discards pending physical keys and an in-flight sequence.
    k.press(80,0);assert(k.read(0)==27);auto epoch=k.generation;k.disconnect();
    assert(k.generation!=epoch && read(k,900).empty() && !k.in_sequence());
    k.press(80,1000);assert(read(k,1000)=="\033[D");k.release(80);
    // App boundaries suppress held keys until release, including boot report replays.
    uint8_t report[8]={0,0,4,0,0,0,0,0};k.boot_report(report,0);assert(read(k,0)=="a");
    k.boundary();k.boot_report(report,500);assert(read(k,1000).empty());
    report[2]=0;k.boot_report(report,1001);report[2]=4;k.boot_report(report,1002);
    assert(read(k,1002)=="a");k.disconnect();
    // Complete boot reports update modifiers even when no key changes.
    k.boot_report(report,0);assert(read(k,0)=="a");report[0]=2;k.boot_report(report,100);
    assert(read(k,400)=="A");report[0]=0;k.boot_report(report,433);assert(read(k,433)=="a");
    // HID rollover error does not leave a repeat running.
    report[2]=1;k.boot_report(report,500);assert(read(k,1000).empty());k.disconnect();
    // No repeat for Enter, Escape, Tab, F keys or lock keys.
    for(uint8_t key:{40,41,43,57,58,69,83,88}) {
        k.press(key,0);read(k,0);assert(read(k,10000).empty());k.release(key);
    }
    // Deadline wrap and one-event-per-consumption scheduling.
    k.press(4,0xffffff00u);assert(read(k,0xffffff00u)=="a");assert(read(k,143).empty());
    assert(read(k,144)=="a");assert(read(k,177)=="a");k.disconnect();
    // Overflow drops whole events and does not arm repeats for a dropped press.
    for(unsigned i=0;i<100;++i) { k.press(80,i);k.release(80); }
    assert(k.dropped>0 && k.queued()==47);auto all=read(k,1000);assert(all.size()==47*3);
    for(size_t i=0;i<all.size();i+=3)assert(all.substr(i,3)=="\033[D");
    assert(encode(4,2)=="A" && encode(4,0,2)=="A" && encode(4,2,2)=="a");
    assert(encode(30,2)=="!" && encode(52,2)=="\"" && encode(56)=="/");
    assert(encode(6,1)=="\003" && encode(48,1)=="\035");
    assert(encode(80,2)=="\033[1;2D" && encode(80,1)=="\033[1;5D");
    assert(encode(76)=="\033[3~" && encode(58)=="\033OP" && encode(69)=="\033[24~");
    assert(encode(89,0,1)=="1" && encode(89)=="\033[F" && encode(98,0,1)=="0");
    // Repeated mixed lifecycle/reconnect workload under sanitizers.
    for(unsigned i=0;i<10000;++i) {
        k.press(4,i*1000);read(k,i*1000);read(k,i*1000+400);
        k.press(80,i*1000+401);read(k,i*1000+401);k.boundary();k.disconnect();
        assert(read(k,i*1000+999).empty());
    }
    puts("PASS: Num Lock initialization/reconnect ordering, repeat timing/wrap, no bursts, modifier changes, newest-key policy, release, disconnect, app boundaries, rollover, atomic overflow, ANSI/keypad mapping and 10,000 lifecycle cycles");
}
