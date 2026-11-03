"""Exercise production input publication/navigation/Game Boy APIs with paired samples."""
from pathlib import Path
import re
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
port = root / 'src/platform/imxrt1062/teensy41'
source = (port / 'small_monitor.h').read_text()
functions = '\n'.join(re.search(r'^extern "C" (?:void|bool|uint8_t|unsigned) ' + name + r'\([\s\S]*?^}', source, re.M)[0]
                      for name in ('sk_small_input_poll', 'sk_small_input_stop', 'sk_small_buttons',
                                   'sk_small_controller_count', 'sk_small_controller_snapshot', 'sk_small_controller_buttons'))
gameboy = re.search(r'^extern "C" uint8_t sk_gameboy_buttons\([\s\S]*?^}', (port / 'peripherals.cpp').read_text(), re.M)[0]
code = r'''
#include <cassert>
#include <cstdint>
#include "small_navigation.h"
#define SK_SECONDARY_ST7735 1
#define SK_USB_HOST 1
#define SK_NES_CONTROLLER_DATA_PIN 6
static unsigned depth=0,notifies=0,reads=0;
#define taskENTER_CRITICAL() (++depth)
#define taskEXIT_CRITICAL() (--depth)
static unsigned small_task=1;
static bool small_owned=false,small_enabled=true,small_console=false;
static uint32_t small_generation,now;
static uint8_t nes_held,small_motion_buttons;
static NesInputs nes_inputs;
static SmallNavigation small_nav;
static struct {bool active;} small_music;
static uint32_t small_music_at;
static bool small_music_toggle;
uint32_t millis(){return now;}
void xTaskNotifyGive(unsigned){++notifies;}
namespace nes_reader {
static bool fresh,stopped;
static uint8_t board,external;
bool poll(uint32_t t,uint8_t &a,uint8_t &b,uint32_t &at) {
 assert(!depth);++reads;
 if(stopped || !fresh)return false;
 fresh=false;a=board;b=external;at=t;return true;
}
void stop(){assert(depth);stopped=true;}
}
struct KeyboardIrqGuard {KeyboardIrqGuard(){++depth;} ~KeyboardIrqGuard(){--depth;}};
struct Keyboard {bool down=false;bool is_down(uint8_t usage){return down && usage==0x1d;}} keyboard_input;
''' + functions + gameboy + r'''
void sample(unsigned a,unsigned b,unsigned t) {
 now=t;nes_reader::board=a;nes_reader::external=b;nes_reader::fresh=true;sk_small_input_poll();assert(!depth);
}
void reset() {
 nes_inputs={};nes_held=0;small_motion_buttons=0;small_nav={};small_owned=false;small_enabled=true;small_console=false;
 nes_reader::stopped=false;keyboard_input.down=false;
}
int main() {
 assert(sk_small_controller_count()==2 && sk_small_controller_buttons(2)==0);
 // Two independent masks, and merged compatibility state for DMM/Game Boy.
 sample(4,2,100);sample(4,2,120);
 uint8_t pair[2];sk_small_controller_snapshot(pair);assert(pair[0]==1 && pair[1]==2);
 assert(sk_small_buttons()==3 && sk_gameboy_buttons()==3);
 keyboard_input.down=true;
 sample(0,2,130);sample(0,2,150);
 assert(sk_small_controller_buttons(0)==0 && sk_small_controller_buttons(1)==2);
 assert(sk_small_buttons()==2 && sk_gameboy_buttons()==3); // keyboard stays separate.
 // Either source navigates, without adding a second debounce interval.
 reset();sample(32,0,100);sample(32,0,120);assert(small_nav.page==1);
 sample(0,0,130);sample(0,0,150);sample(0,64,160);sample(0,64,180);assert(small_nav.page==0);
 // Music A works from either source, once on an edge.
 for(unsigned player=0;player<2;player++) {
  reset();small_nav.page=2;small_music.active=true;small_music_at=100;
  sample(player?0:4,player?1:0,100);sample(player?0:4,player?1:0,120);
  assert(small_music_toggle);small_music_toggle=false;
  sample(player?0:4,player?1:0,140);assert(!small_music_toggle);
 }
 // Apps own the screen, but both physical source states keep updating.
 reset();small_owned=true;sample(0,128,100);sample(0,128,120);
 assert(small_nav.page==0 && sk_small_controller_buttons(1)==16 && sk_gameboy_buttons()==16);
 small_owned=false;sample(0,128,130);assert(small_nav.page==0); // no phantom navigation
 // Bad/grounded stream cannot suppress its healthy partner.
 reset();sample(255,2,100);sample(255,2,120);assert(sk_small_buttons()==2);
 sample(4,255,130);sample(4,255,150);assert(sk_small_buttons()==1);
 // Sensor-page actions queue only edges and respect display ownership.
 reset();small_nav.page=5;sample(4,0,100);sample(4,0,120);assert(small_motion_buttons==1);
 small_motion_buttons=0;sample(4,0,140);assert(!small_motion_buttons);
 sample(0,0,160);sample(0,0,180);sample(0,2,200);sample(0,2,220);assert(small_motion_buttons==2);
 reset();small_nav.page=6;small_owned=true;sample(4,0,100);sample(4,0,120);assert(!small_motion_buttons);
 reset();small_nav.page=6;sample(4,0,100);sample(4,0,120);assert(small_motion_buttons==1);
 sample(32,0,140);sample(32,0,160);assert(small_nav.page==0 && !small_motion_buttons);
 sk_small_input_stop();assert(sk_small_buttons()==0);
 sk_small_controller_snapshot(pair);assert(!pair[0] && !pair[1]);
 sample(1,2,200);assert(sk_small_buttons()==0);
 sk_small_controller_snapshot(nullptr);assert(!depth);
}
'''
with tempfile.TemporaryDirectory(prefix='solaros-dual-input-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I'+str(port),
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('PASS: paired physical APIs, independent debounce, either-source navigation/music, Game Boy/keyboard merge, ownership and shutdown')
