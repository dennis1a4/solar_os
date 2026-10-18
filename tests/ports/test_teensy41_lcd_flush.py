"""Exercise the actual bounded flush against the terminal and a mock display."""
from pathlib import Path
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
port=root/'src/platform/imxrt1062/teensy41'
source=(port/'peripherals.cpp').read_text()
functions='\n'.join(re.search(r'^(?:void|bool) '+name+r'\([\s\S]*?^}',source,re.M)[0]
                    for name in ('sk_lcd_write','sk_lcd_configure','sk_lcd_flush'))
code=r'''
#include <cassert>
#include <cstring>
#include "lcd_terminal.h"
#define SK_PLOT 1
#define SK_SECONDARY_ST7735 1
namespace superkeyboard { constexpr unsigned secondary_spi=1; }
static bool lcd_small=false,small_owned=false;
static LcdTerminal terminal;
static LcdTerminal *lcd=&terminal;
static bool graphics_mode=false,lcd_repaint=true,lcd_pending=true;
static unsigned render_position=0,locks=0,draws=0;
static int lcd_mutex,held=0;
static constexpr int portMAX_DELAY=0;
static bool spi=false,allow_spi=true;
void xSemaphoreTake(int,int){assert(!held);held=1;}
void xSemaphoreGive(int){assert(held);held=0;}
bool sk_spi_lock(unsigned bus){assert(bus==(lcd_small?1u:0u));assert(!held && !spi);++locks;return spi=allow_spi;}
void sk_spi_unlock(unsigned bus){assert(bus==(lcd_small?1u:0u));assert(spi && !held);spi=false;}
struct Display {
 void setTextSize(unsigned){} void setTextWrap(bool){} void fillScreen(unsigned){}
 void drawFastHLine(unsigned,unsigned,unsigned,unsigned){}
 void setFontDefault(){} void setFontScale(unsigned){} void clearScreen(unsigned){}
 void setTextColor(unsigned,unsigned){} void setCursor(unsigned,unsigned){}
 void write(unsigned){assert(held && spi);++draws;}
 void drawLine(unsigned,unsigned,unsigned,unsigned,unsigned){}
} primary,secondary;
''' + functions + r'''
static void drain(){
 for(unsigned n=0;lcd_pending || lcd_repaint;++n){
  assert(n<30);unsigned before=draws;sk_lcd_flush();assert(draws-before<=128);
 }
 for(unsigned y=0;y<lcd->rows;++y)for(unsigned x=0;x<lcd->cols;++x)assert(!lcd->dirty[y][x]);
 unsigned before=locks;for(unsigned i=0;i<100;++i)sk_lcd_flush();assert(locks==before);
}
int main(){
 lcd->reset();drain();assert(draws==3000);
 const char *sequences[]={"Hello", "\033[2;10H", "\033[?25l", "\033[?25h", "\033[2J", "\033[30;1H\n"};
 for(auto text:sequences){sk_lcd_write(text,strlen(text));assert(lcd_pending);drain();}
 sk_lcd_write("x",1);allow_spi=false;sk_lcd_flush();assert(lcd_pending);allow_spi=true;drain();
 assert(sk_lcd_configure(2,7,0));assert(lcd_repaint);drain();
 graphics_mode=true;sk_lcd_write("z",1);unsigned before=locks;sk_lcd_flush();assert(locks==before && lcd_pending);
 graphics_mode=false;lcd_repaint=true;drain();
 lcd_small=true;assert(lcd->geometry(26,16));assert(sk_lcd_configure(1,7,0));
 small_owned=true;unsigned covered=draws;sk_lcd_flush();assert(draws==covered && lcd_repaint);
 small_owned=false;
 unsigned small_before=draws;drain();assert(draws-small_before==26*16);
 sk_lcd_write("\033[1;4;7;31mSmall console\r\n",strlen("\033[1;4;7;31mSmall console\r\n"));drain();
 sk_lcd_write("\033[16;1Hlast\r\nnext",strlen("\033[16;1Hlast\r\nnext"));drain();
 allow_spi=false;sk_lcd_write("x",1);sk_lcd_flush();assert(lcd_pending);allow_spi=true;drain();
 assert(sk_lcd_configure(2,7,0));drain();assert(lcd->cols==13 && lcd->rows==8);
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(port),str(p/'test.cpp'),str(port/'lcd_terminal.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: idle flush skips SPI; bounded redraw, cursor, erase, scroll, resize, busy SPI and graphics recovery')
