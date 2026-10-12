"""Host checks of the production NES waveform and asynchronous driver.
Hardware register timing still requires a logic analyzer on a Teensy.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PORT = ROOT / 'src/platform/imxrt1062/teensy41'
CORE = Path.home() / '.platformio/packages/framework-arduinoteensy-ts/teensy4/imxrt.h'
# Use the actual register encodings supplied to the firmware, not invented values.
lines = CORE.read_text().splitlines()
defines = []
for i, line in enumerate(lines):
    if line.startswith('#define FLEXIO_'):
        defines.append(line)
        while lines[i].endswith('\\'):
            i += 1
            defines.append(lines[i])

mock = r'''
#pragma once
#include <cstdint>
#include <cassert>
#include <cstring>
#include <initializer_list>
#define DMAMEM
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT_PULLUP 2
namespace superkeyboard { constexpr unsigned shift_clock=2,nes_latch=3,nes_data=4; }
static uint32_t tick=100, delays=0, hz=30000000;
static bool allocation=true, available=true;
static unsigned pins[5]{}, mux[5]{};
uint32_t millis(){return tick;}
void digitalWrite(unsigned p,unsigned v){pins[p]=v;}
int digitalRead(unsigned){return 1;}
void pinMode(unsigned p,unsigned){mux[p]=0;}
void delayMicroseconds(unsigned n){delays+=n;}
void arm_dcache_delete(void *p,unsigned n){assert(uintptr_t(p)%32==0 && n==32);}
void arm_dcache_flush_delete(void *p,unsigned n){arm_dcache_delete(p,n);}
struct W1C {
 uint32_t value=0;
 operator uint32_t() const {return value;}
 void operator=(uint32_t bits){value &= ~bits;}
};
struct Registers {
 uint32_t TIMCTL[8]{},TIMCFG[8]{},TIMCMP[8]{},SHIFTCTL[8]{},SHIFTCFG[8]{};
 volatile uint32_t SHIFTBUF[8]{};
 uint32_t SHIFTSDEN=0,CTRL=0;
 W1C SHIFTERR,TIMSTAT;
} regs;
class FlexIOHandler {
public:
 unsigned timers=0,shifters=0;
 static FlexIOHandler *mapIOPinToFlexIOHandler(unsigned,uint8_t &pin);
 uint8_t mapIOPinToFlexPin(unsigned p){return p+2;}
 Registers &port(){return regs;}
 uint32_t computeClockRate(){return hz;}
 void setClockSettings(unsigned a,unsigned b,unsigned c){assert(a==3 && b==3 && c==3);hz=30000000;}
 uint8_t requestTimers(){for(unsigned i=0;i<8;++i)if(!(timers&(1<<i))){timers|=1<<i;return i;}return 255;}
 bool claimShifter(unsigned i){if(shifters&(1<<i))return false;shifters|=1<<i;return true;}
 uint8_t requestShifter(){for(unsigned i=0;i<8;++i)if(claimShifter(i))return i;return 255;}
 uint8_t shiftersDMAChannel(unsigned i){return i<4?i:255;}
 void setIOPinToFlexMode(unsigned p){mux[p]=1;}
} handler;
FlexIOHandler *FlexIOHandler::mapIOPinToFlexIOHandler(unsigned,uint8_t &p){p=4;return available?&handler:nullptr;}
class DMAChannel {
public:
 void *TCD=nullptr;
 bool done=false,enabled=false;
 explicit DMAChannel(bool){}
 void begin(bool){if(allocation)TCD=this;}
 void disable(){enabled=false;}
 void enable(){enabled=true;}
 bool complete(){return done;}
 void clearComplete(){done=false;}
 void source(volatile uint32_t &){}
 void destination(uint32_t &){}
 void transferSize(unsigned n){assert(n==4);}
 void transferCount(unsigned n){assert(n==1);}
 void disableOnCompletion(){}
 void triggerAtHardwareEvent(unsigned n){assert(n<4);}
};
'''
checks = r'''
#include "nes_flexio.h"
#include <cstdio>
// Model a latched NES shift register independently of the decoder. Sample at
// the center of each output slot, after any rising clock edge has shifted it.
static uint32_t capture(unsigned buttons) {
 uint32_t samples=0;
 unsigned state=0,edges=0;
 bool old_clock=false;
 for(unsigned slot=0;slot<32;++slot) {
  bool latch=(nes_waveform::latch>>slot)&1;
  bool clock=(nes_waveform::clock>>slot)&1;
  if(latch)state=(~buttons)&255;
  else if(clock && !old_clock){state=(state>>1)|128;++edges;}
  if(state&1)samples|=1UL<<slot;
  old_clock=clock;
 }
 assert(edges==8 && !old_clock);
 return samples;
}
int main(int argc,char **argv) {
 assert(argc==2);
 for(unsigned b=0;b<256;++b) assert(nes_waveform::decode(capture(b))==b);
 assert(nes_waveform::divider(30000000)==90);
 assert(nes_waveform::divider(30000001)==91);
 assert(nes_waveform::divider(0)==0);
 uint8_t raw=0;uint32_t at=0;
 if(!strcmp(argv[1],"dma-fail"))allocation=false;
 if(!strcmp(argv[1],"clock-fail"))hz=120000000;
 if(!strcmp(argv[1],"resource-fail"))handler.shifters=255;
 if(!strcmp(argv[1],"no-flex"))available=false;
 // Reserve nonzero resources belonging to another driver, and check they
 // survive every success, failure and shutdown path.
 handler.timers|=2;handler.shifters|=128;
 regs.TIMCTL[1]=0xabcdef;regs.SHIFTCTL[7]=0xfedcba;
 if(!strcmp(argv[1],"cold")) {
  regs.TIMCTL[1]=regs.SHIFTCTL[7]=0;
  hz=120000000;
 }
 nes_reader::begin();
 if(!strcmp(argv[1],"cold")) {
  assert(hz==30000000 && !strcmp(nes_reader::mode,"flexio-dma"));
  nes_reader::stop();return 0;
 }
 if(!strcmp(argv[1],"normal") || !strcmp(argv[1],"timeout") || !strcmp(argv[1],"error")) {
  assert(!strcmp(nes_reader::mode,"flexio-dma"));
  assert(mux[2] && mux[3] && mux[4]);
  assert(regs.TIMCMP[nes_reader::timer]==0x3f59);
  assert(!nes_reader::poll(tick,raw,at) && nes_reader::running && !delays);
  assert(regs.SHIFTBUF[nes_reader::tx_clock]==nes_waveform::clock);
  assert(regs.SHIFTBUF[nes_reader::tx_latch]==nes_waveform::latch);
  assert(!nes_reader::poll(tick+1,raw,at));
  if(!strcmp(argv[1],"timeout")) {
   assert(!nes_reader::poll(tick+5,raw,at));
   assert(!strcmp(nes_reader::mode,"gpio-timeout") && nes_reader::faults==1);
   assert(nes_reader::poll(tick+10,raw,at) && delays==114 && raw==0);
  } else {
   nes_reader::sample[0]=capture(0x95);
   nes_reader::dma.done=true;
   if(!strcmp(argv[1],"error"))regs.SHIFTERR.value=1UL<<nes_reader::rx;
   bool result=nes_reader::poll(tick+2,raw,at);
   if(!strcmp(argv[1],"error")) {
    assert(!result && nes_reader::faults==1 && !strcmp(nes_reader::mode,"gpio-error"));
   } else {
    assert(result && raw==0x95 && at==tick && nes_reader::scans==1);
    assert(!nes_reader::dma.enabled && !nes_reader::running && !delays);
    assert(!nes_reader::poll(tick+9,raw,at));
    assert(!nes_reader::poll(tick+10,raw,at) && nes_reader::running);
    // Completed DMA is accepted even when the consumer was delayed >5 ms.
    nes_reader::sample[0]=capture(0x42);nes_reader::dma.done=true;
    assert(nes_reader::poll(tick+30,raw,at) && raw==0x42 && at==tick+10);
    // Millisecond wrap retains the periodic cadence.
    nes_reader::running=false;nes_reader::sampled=0xfffffffc;
    assert(!nes_reader::poll(5,raw,at) && !nes_reader::running);
    assert(!nes_reader::poll(6,raw,at) && nes_reader::running);
   }
  }
 } else {
  assert(!nes_reader::flex && !mux[2] && !mux[3] && !mux[4]);
  assert(nes_reader::poll(tick,raw,at) && raw==0 && delays==114);
  assert(!nes_reader::poll(tick+9,raw,at));
 }
 nes_reader::stop();
 assert(!nes_reader::running && !nes_reader::dma.enabled);
 assert(!pins[2] && !pins[3] && !mux[2] && !mux[3]);
 assert(!nes_reader::poll(tick+100,raw,at));
 assert(regs.TIMCTL[1]==0xabcdef && regs.SHIFTCTL[7]==0xfedcba);
 assert(handler.timers&2 && handler.shifters&128);
 puts(argv[1]);
}
'''
with tempfile.TemporaryDirectory(prefix='solaros-nes-flexio-') as directory:
    p = Path(directory)
    (p/'FlexIO_t4.h').write_text(mock + '\n' + '\n'.join(defines))
    (p/'DMAChannel.h').write_text('#pragma once\n')
    (p/'test.cpp').write_text(checks)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(p), '-I'+str(PORT), str(p/'test.cpp'),
                    '-o', str(p/'test')], check=True)
    for case in ('normal', 'cold', 'timeout', 'error', 'dma-fail', 'clock-fail', 'resource-fail', 'no-flex'):
        subprocess.run([str(p/'test'), case], check=True)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-DSK_NES_FLEXIO=0', '-I'+str(p), '-I'+str(PORT),
                    str(p/'test.cpp'), '-o', str(p/'gpio')], check=True)
    subprocess.run([str(p/'gpio'), 'disabled'], check=True)
print('PASS: all 256 NES states, DMA lifecycle, cadence/wrap, fallback and shutdown isolation')
