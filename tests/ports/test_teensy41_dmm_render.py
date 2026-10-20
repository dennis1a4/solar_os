"""Exercise the real incremental TFT renderer and assert no refresh blanking."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
port = root / 'src/platform/imxrt1062/teensy41'
source = (port / 'small_monitor.h').read_text()
render = re.search(r'^bool sk_small_dmm_frame\([\s\S]*?^}', source, re.M)[0]
code = r'''
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include "dmm_model.h"
#include "dmm_paint.h"
enum { ST7735_BLACK, ST7735_YELLOW, ST7735_CYAN, ST7735_WHITE, ST7735_BLUE, ST7735_GREEN };
namespace superkeyboard { constexpr unsigned secondary_spi=1; }
static bool small_owned=true,allow_spi=true,locked=false;
static uint32_t small_owner_generation=1,test_now=10000;
static unsigned operations=0,screen_clears=0,glyphs=0,large_glyphs=0,static_glyphs=0;
uint32_t millis(){return test_now;}
bool sk_spi_lock(unsigned bus){assert(bus==1 && !locked);return locked=allow_spi;}
void sk_spi_unlock(unsigned bus){assert(bus==1 && locked);locked=false;}
struct Display {
 unsigned size=1,x=0,y=0;
 uint16_t pixels[128][160]{};
 char cells[128][160]{};
 void setTextWrap(bool){}
 void setTextSize(unsigned n){assert(n>=1 && n<=3);size=n;}
 void fillScreen(unsigned){assert(locked);++operations;++screen_clears;memset(pixels,0,sizeof(pixels));memset(cells,0,sizeof(cells));}
 void setTextColor(unsigned,unsigned){}
 void setCursor(unsigned px,unsigned py){assert(px<160 && py<128);x=px;y=py;}
 void write(uint8_t ch){
  assert(locked && x+6*size<=160 && y+8*size<=128);
  cells[y][x]=ch;x+=6*size;++operations;++glyphs;
  if(size==3)++large_glyphs;
  if(y==66 || y==76 || y==43)++static_glyphs;
 }
 void print(const char *s){while(*s)write(*s++);}
 void drawFastHLine(int x,int y,int w,unsigned ink){
  assert(locked && x>=0 && y>=0 && x+w<=160 && y<128 && w>0);
  for(int i=0;i<w;++i)pixels[y][x+i]=ink;
  ++operations;
 }
 void drawRect(int x,int y,int w,int h,unsigned ink){
  assert(locked && x>=0 && y>=0 && x+w<=160 && y+h<=128);
  for(int row=y;row<y+h;++row)for(int col=x;col<x+w;++col)
   if(row==y || row==y+h-1 || col==x || col==x+w-1)pixels[row][col]=ink;
  ++operations;
 }
 std::string line(unsigned x,unsigned y,unsigned count,unsigned scale=1){
  std::string out;for(unsigned i=0;i<count;++i){char ch=cells[y][x+i*6*scale];out+=ch?ch:' ';}return out;
 }
 unsigned trace_pixels(){unsigned n=0;for(int y=89;y<=115;++y)for(int x=2;x<=157;++x)if(pixels[y][x]==ST7735_GREEN)++n;return n;}
} secondary;
''' + render + r'''
int main(){
 DmmModel model;
 for(unsigned i=0;i<=200;++i)model.sample(i*50,1000+(i%50)*50);
 assert(sk_small_dmm_frame(&model,true) && !locked);
 assert(secondary.line(2,1,26).find("DEMO DC 5V")!=std::string::npos);
 assert(secondary.trace_pixels()>150 && screen_clears==0);
 unsigned before=operations,initial_guides=static_glyphs;
 assert(sk_small_dmm_frame(&model,true));assert(operations==before); // identical frame writes nothing
 model.toggle_hold();model.sample(10050,4000);test_now=10050;
 unsigned held_digits=large_glyphs;
 assert(sk_small_dmm_frame(&model,true));assert(secondary.line(2,13,26).find("HOLD")!=std::string::npos);
 assert(large_glyphs==held_digits && static_glyphs==initial_guides); // live trace/extrema, stable held digits
 model.set_mode(DmmModel::Live);
 model.sample(10550,2345);test_now=10550;
 assert(sk_small_dmm_frame(&model,true));assert(secondary.line(2,27,7,3)=="2.345  ");
 // Only the last digit changes. No label, background or other digit is redrawn.
 held_digits=large_glyphs;
 model.sample(10550,2346);assert(sk_small_dmm_frame(&model,true));
 assert(large_glyphs-held_digits==1 && static_glyphs==initial_guides && screen_clears==0);
 model.set_range(50);model.sample(10600,50000,true);test_now=10600;
 assert(sk_small_dmm_frame(&model,false));
 assert(secondary.line(2,1,26).find("UNCAL DC 50V")!=std::string::npos);
 assert(secondary.line(2,27,7,3)=="OL     "); // shorter field erases trailing glyphs
 test_now+=15000;assert(sk_small_dmm_frame(&model,true));
 assert(secondary.trace_pixels()==0 && secondary.line(2,13,26).find("STALE")!=std::string::npos);
 // Trace erasure cannot disturb the permanent border or controls.
 assert(secondary.pixels[87][1]==ST7735_BLUE && static_glyphs==initial_guides && screen_clears==0);
 before=operations;small_owned=false;assert(!sk_small_dmm_frame(&model,true) && !locked);
 small_owned=true;allow_spi=false;assert(!sk_small_dmm_frame(&model,true));assert(operations==before);
 model=DmmModel{};allow_spi=true;assert(sk_small_dmm_frame(&model,true));
 assert(secondary.line(2,27,7,3)=="---    ");
 // A newly acquired surface was cleared by the ownership API; reset the cache
 // so static labels and an otherwise identical readout are painted again.
 locked=true;secondary.fillScreen(0);locked=false;++small_owner_generation;
 before=operations;assert(sk_small_dmm_frame(&model,true));assert(operations>before);
 assert(secondary.line(2,66,26).find("A hold B mode L graph")!=std::string::npos);
 assert(secondary.line(2,27,7,3)=="---    ");assert(screen_clears==1);
 model=DmmModel{};model.test=true;model.sample(test_now,3000);
 locked=true;secondary.fillScreen(0);locked=false;++small_owner_generation;
 assert(sk_small_dmm_frame(&model,false));
 assert(secondary.line(2,1,26).find("TEST UNCAL 5V")!=std::string::npos);
 assert(secondary.line(2,76,26).find("SEL reset")!=std::string::npos);
 // Fixed-range rendering clips out-of-range samples at the plot edges and
 // changes only the trace/footer, without clearing history or the readout.
 model=DmmModel{};model.sample(test_now-100,1000);model.sample(test_now-50,4000);model.sample(test_now,3000);
 assert(sk_small_dmm_frame(&model,true));
 assert(secondary.line(2,120,26).find("AUTO")!=std::string::npos);
 unsigned number_writes=large_glyphs;model.trace_auto=false;
 assert(sk_small_dmm_frame(&model,true));
 assert(secondary.line(2,120,26).find("0-5V")!=std::string::npos && large_glyphs==number_writes);
 model.sample(test_now,-1000);model.sample(test_now,9000);assert(sk_small_dmm_frame(&model,true));
 assert(secondary.pixels[89][157]==ST7735_GREEN);
 model.set_range(50);model.sample(test_now,25000);assert(!model.trace_auto);
 assert(sk_small_dmm_frame(&model,true));assert(secondary.line(2,120,26).find("0-50V")!=std::string::npos);
 // Timer rollover preserves bounds, the visible ten-second axis and font state.
 model=DmmModel{};const uint32_t start=UINT32_MAX-5000;
 for(unsigned i=0;i<=200;++i)model.sample(start+i*50,1000+(i%20)*100);
 test_now=start+10000;assert(sk_small_dmm_frame(&model,true));
 assert(secondary.size==1 && secondary.trace_pixels()>0 && screen_clears==2);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-I'+str(port), str(tmp / 'test.cpp'),
                    '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
print('PASS: incremental DMM glyphs/trace, zero refresh clears, idle/hold, shortened fields, stale erase and new leases')
