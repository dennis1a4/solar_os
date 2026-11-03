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
#ifndef SK_NES_CONTROLLER_DATA_PIN
#define SK_NES_CONTROLLER_DATA_PIN -1
#endif
namespace superkeyboard { constexpr unsigned shift_clock=2,nes_latch=3,nes_data=4; constexpr int nes_controller_data=SK_NES_CONTROLLER_DATA_PIN; }
static uint32_t irq_state;
uint32_t __get_PRIMASK(){return irq_state;}
void __disable_irq(){irq_state=1;}
void __enable_irq(){irq_state=0;}
static uint32_t tick=100, delays=0, hz=30000000;
static bool allocation=true, available=true,second_allocation=true,second_available=true,same_flex=false;
static unsigned dma_allocations;
static uint32_t second_hz=30000000;
static unsigned pins[42]{}, mux[42]{},gpio_bits[2]{},gpio_index;
uint32_t millis(){return tick;}
void digitalWrite(unsigned p,unsigned v){if(p==2 && pins[p] && !v)++gpio_index;pins[p]=v;}
int digitalRead(unsigned pin){return !((gpio_bits[pin==4?0:1]>>gpio_index)&1); }
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
} regs,second_regs;
class FlexIOHandler {
public:
 unsigned timers=0,shifters=0;bool second=false;
 static FlexIOHandler *mapIOPinToFlexIOHandler(unsigned,uint8_t &pin);
 uint8_t mapIOPinToFlexPin(unsigned p){return p+2;}
 Registers &port(){return second?second_regs:regs;}
 uint32_t computeClockRate(){return second?second_hz:hz;}
 void setClockSettings(unsigned a,unsigned b,unsigned c){assert(a==3 && b==3 && c==3);(second?second_hz:hz)=30000000;}
 uint8_t requestTimers(){for(unsigned i=0;i<8;++i)if(!(timers&(1<<i))){timers|=1<<i;return i;}return 255;}
 bool claimShifter(unsigned i){if(shifters&(1<<i))return false;shifters|=1<<i;return true;}
 uint8_t requestShifter(){for(unsigned i=0;i<8;++i)if(claimShifter(i))return i;return 255;}
 uint8_t shiftersDMAChannel(unsigned i){return i<4?i:255;}
 void setIOPinToFlexMode(unsigned p){mux[p]=1;}
} handler,second_handler;
FlexIOHandler *FlexIOHandler::mapIOPinToFlexIOHandler(unsigned pin,uint8_t &p){p=pin==2?4:10;return pin==2?(available?&handler:nullptr):(second_available?(same_flex?&handler:&second_handler):nullptr);}
class DMAChannel {
public:
 void *TCD=nullptr;
 bool done=false,enabled=false;
 explicit DMAChannel(bool){}
 void begin(bool){if(dma_allocations++==0?allocation:second_allocation)TCD=this;}
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
 uint8_t raw=0,external=0;uint32_t at=0;
 second_handler.second=true;
 if(!strcmp(argv[1],"second-dma-fail"))second_allocation=false;
 if(!strcmp(argv[1],"second-flex-fail"))second_available=false;
 if(!strcmp(argv[1],"second-resource-fail"))second_handler.shifters=255;
 if(!strcmp(argv[1],"second-clock-fail"))second_hz=120000000;
 if(!strcmp(argv[1],"phase-fail"))second_hz=24000001;
 if(!strcmp(argv[1],"same-flex"))same_flex=true;
 if(!strcmp(argv[1],"dma-fail"))allocation=false;
 if(!strcmp(argv[1],"clock-fail"))hz=120000000;
 if(!strcmp(argv[1],"resource-fail"))handler.shifters=255;
 if(!strcmp(argv[1],"no-flex"))available=false;
 // Reserve nonzero resources belonging to another driver, and check they
 // survive every success, failure and shutdown path.
 handler.timers|=2;handler.shifters|=128;
 regs.TIMCTL[1]=0xabcdef;regs.SHIFTCTL[7]=0xfedcba;
 second_regs.TIMCTL[1]=0x112233;second_regs.SHIFTCTL[7]=0x445566;
 second_handler.timers|=2;second_handler.shifters|=128;
 if(!strcmp(argv[1],"cold")) {
  regs.TIMCTL[1]=regs.SHIFTCTL[7]=0;
  hz=120000000;second_regs.TIMCTL[1]=second_regs.SHIFTCTL[7]=0;second_hz=120000000;
 }
 const char *expected_mode=SK_NES_CONTROLLER_DATA_PIN>=0?"flexio-dual-dma":"flexio-dma";
 nes_reader::begin();
 if(!strcmp(argv[1],"cold")) {
  assert(hz==30000000 && !strcmp(nes_reader::mode,expected_mode));
  nes_reader::stop();return 0;
 }
 if(!strcmp(argv[1],"normal") || !strcmp(argv[1],"timeout") || !strcmp(argv[1],"error") || !strcmp(argv[1],"second-error") || !strcmp(argv[1],"second-timeout") || !strcmp(argv[1],"same-flex")) {
  assert(!strcmp(nes_reader::mode,expected_mode));
  assert(mux[2] && mux[3] && mux[4]);
  assert(regs.TIMCMP[nes_reader::timer]==0x3f59);
#if SK_NES_CONTROLLER_DATA_PIN >= 0
  if(!same_flex) {
   assert(second_regs.TIMCMP[nes_reader::second_timer]==0x3f59);
   assert(!(second_regs.SHIFTCTL[nes_reader::second_trigger] & FLEXIO_SHIFTCTL_PINMODE_OUTPUT));
  }
#endif
  assert(!nes_reader::poll(tick,raw,external,at) && nes_reader::running && !delays);
  assert(regs.SHIFTBUF[nes_reader::tx_clock]==nes_waveform::clock);
  assert(regs.SHIFTBUF[nes_reader::tx_latch]==nes_waveform::latch);
  assert(!nes_reader::poll(tick+1,raw,external,at));
  if(!strcmp(argv[1],"timeout")) {
   assert(!nes_reader::poll(tick+5,raw,external,at));
   assert(!strcmp(nes_reader::mode,"gpio-timeout") && nes_reader::faults==1);
   assert(nes_reader::poll(tick+10,raw,external,at) && delays==114 && raw==0);
  } else {
   nes_reader::sample[0]=capture(0x95);
   nes_reader::dma.done=true;
#if SK_NES_CONTROLLER_DATA_PIN >= 0
   nes_reader::second_sample[0]=capture(0x62);
   nes_reader::second_dma.done=strcmp(argv[1],"second-timeout");
   if(!strcmp(argv[1],"second-error"))second_regs.SHIFTERR.value=1UL<<nes_reader::second_rx;
   if(!strcmp(argv[1],"second-timeout")) {
    assert(!nes_reader::poll(tick+5,raw,external,at));
    assert(nes_reader::faults==1 && !strcmp(nes_reader::mode,"gpio-timeout"));
    nes_reader::stop();return 0;
   }
#endif
   if(!strcmp(argv[1],"error"))regs.SHIFTERR.value=1UL<<nes_reader::rx;
   bool result=nes_reader::poll(tick+2,raw,external,at);
   if(!strcmp(argv[1],"error") || !strcmp(argv[1],"second-error")) {
    assert(!result && nes_reader::faults==1 && !strcmp(nes_reader::mode,"gpio-error"));
   } else {
    assert(result && raw==0x95 && at==tick && nes_reader::scans==1);
    assert(external==(SK_NES_CONTROLLER_DATA_PIN>=0?0x62:0));
    assert(irq_state==0);
#if SK_NES_CONTROLLER_DATA_PIN >= 0
    assert(mux[SK_NES_CONTROLLER_DATA_PIN] && !nes_reader::second_dma.enabled);
#endif
    assert(!nes_reader::dma.enabled && !nes_reader::running && !delays);
    assert(!nes_reader::poll(tick+9,raw,external,at));
    assert(!nes_reader::poll(tick+10,raw,external,at) && nes_reader::running);
    // Completed DMA is accepted even when the consumer was delayed >5 ms.
    nes_reader::sample[0]=capture(0x42);nes_reader::dma.done=true;
#if SK_NES_CONTROLLER_DATA_PIN >= 0
    nes_reader::second_dma.done=true;
#endif
    assert(nes_reader::poll(tick+30,raw,external,at) && raw==0x42 && at==tick+10);
    // Millisecond wrap retains the periodic cadence.
    nes_reader::running=false;nes_reader::sampled=0xfffffffc;
    assert(!nes_reader::poll(5,raw,external,at) && !nes_reader::running);
    irq_state=1;
    assert(!nes_reader::poll(6,raw,external,at) && nes_reader::running);
    assert(irq_state==1);irq_state=0;
   }
  }
 } else {
  assert(!nes_reader::flex && !mux[2] && !mux[3] && !mux[4]);
  assert(nes_reader::poll(tick,raw,external,at) && raw==0 && delays==114);
  assert(!nes_reader::poll(tick+9,raw,external,at));
 }
 nes_reader::stop();
 assert(!nes_reader::running && !nes_reader::dma.enabled);
#if SK_NES_CONTROLLER_DATA_PIN >= 0
 assert(!nes_reader::second_dma.enabled && !mux[SK_NES_CONTROLLER_DATA_PIN]);
#endif
 assert(!pins[2] && !pins[3] && !mux[2] && !mux[3]);
 assert(!nes_reader::poll(tick+100,raw,external,at));
 assert(regs.TIMCTL[1]==0xabcdef && regs.SHIFTCTL[7]==0xfedcba);
 assert(handler.timers&2 && handler.shifters&128);
 assert(second_regs.TIMCTL[1]==0x112233 && second_regs.SHIFTCTL[7]==0x445566);
 // GPIO fallback samples independent streams during the SAME eight pulses.
 gpio_index=0;gpio_bits[0]=0x95;gpio_bits[1]=0x62;
 uint8_t other=0;
 assert(nes_reader::gpio_read(other)==0x95);
 assert(other==(SK_NES_CONTROLLER_DATA_PIN>=0?0x62:0) && gpio_index==8);
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
                    '-DSK_NES_CONTROLLER_DATA_PIN=6', '-I'+str(p), '-I'+str(PORT),
                    str(p/'test.cpp'), '-o', str(p/'dual')], check=True)
    for case in ('normal','cold','same-flex','timeout','error','second-error','second-timeout',
                 'second-dma-fail','second-flex-fail','second-resource-fail','second-clock-fail','phase-fail'):
        subprocess.run([str(p/'dual'),case],check=True)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-DSK_NES_FLEXIO=0', '-I'+str(p), '-I'+str(PORT),
                    str(p/'test.cpp'), '-o', str(p/'gpio')], check=True)
    subprocess.run([str(p/'gpio'), 'disabled'], check=True)
print('PASS: all 256 NES states on two streams, paired DMA lifecycle, matched clocks, error/timeout fallback, IRQ preservation and shutdown isolation')
