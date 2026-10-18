#include "lcd_terminal.h"
#include "display_probe.h"
#include <cassert>
#include <cstring>
#include <cstdio>
static void put(LcdTerminal &t,const char *s) { t.write(s,strlen(s)); }
struct ProbeDisplay {
    int mode=0,reads=0; int16_t x=0,y=0;
    unsigned errorCode() { return mode==4?1:0; }
    void setFontDefault() {}
    void setCursor(int16_t a,int16_t b) { x=a;y=b; }
    void getCursor(int16_t &a,int16_t &b) {
        ++reads;
        a=mode==1?0:mode==2?-1:mode==3?37:x;
        b=mode==1?0:mode==2?-1:mode==3?53:y;
    }
};
int main() {
    for(int mode=0;mode<=4;++mode) {
        ProbeDisplay display; display.mode=mode;
        assert(sk_primary_display_present(display)==(mode==0));
        assert(display.x==0 && display.y==0);
        assert(display.reads==(mode==4?0:2));
    }
    LcdTerminal t; t.reset();
    put(t,"abc\rZ"); assert(t.cells[0][0].ch=='Z' && t.cells[0][1].ch=='b');
    put(t,"\033[2;4Hhi\033[K"); assert(t.y==1 && t.x==5 && t.cells[1][3].ch=='h');
    put(t,"\033[s\033[30;1Hfooter\033[u"); assert(t.y==1 && t.x==5);
    put(t,"\033[1;29r\033[29;1Hbottom\r\nnext");
    assert(t.cells[27][0].ch=='b' && t.cells[28][0].ch=='n' && t.cells[29][0].ch=='f');
    put(t,"\033[2J\033[H\033[1;4;7;31mX");
    assert(t.cells[0][0].ch=='X' && t.cells[0][0].fg==1 && t.cells[0][0].flags==7);
    put(t,"\033[0m\033[?25l"); assert(!t.visible && !t.flags && t.fg==LcdTerminal::default_color);
    put(t,"\033[999999999;999999999H"); assert(t.y==29 && t.x==99);
    t.reset(); for(unsigned i=0;i<100;++i) t.feed('a');
    assert(t.y==0 && t.x==99); t.feed('b'); assert(t.y==1 && t.cells[1][0].ch=='b');
    put(t,"\033[2K"); for(auto c:t.cells[1]) assert(c.ch==' ');
    t.reset(); put(t,"\033]0;title\007OK"); assert(t.cells[0][0].ch=='O');
    put(t,"\xe2\x94\x80!"); assert(t.cells[0][2].ch=='?' && t.cells[0][3].ch=='!');
    // Help can open while a selected menu row has inverse styling active.
    // Erased space must not inherit that highlight; explicitly drawn space must.
    t.reset(); put(t,"\033[7m\033[2J\033[H ");
    assert(t.cells[0][0].flags&4);
    assert(t.cells[1][0].ch==' ' && t.cells[1][0].flags==0);
    put(t,"\033[27m\033[2;1HHelp text");
    assert(t.cells[1][9].flags==0 && t.cells[29][99].flags==0);
    // Erase-line and newly scrolled-in rows also discard text decorations,
    // while retaining explicitly selected foreground/background colors.
    put(t,"\033[1;4;7;31;44m\033[3;1H\033[2K");
    assert(t.cells[2][0].flags==0 && t.cells[2][0].fg==1 && t.cells[2][0].bg==4);
    put(t,"\033[30;1H\n");
    assert(t.cells[29][0].flags==0 && t.cells[29][0].bg==4);
    // Malformed/random input must never index outside the terminal buffer.
    unsigned rnd=123456;
    for(unsigned i=0;i<200000;++i) { rnd=rnd*1664525u+1013904223u; t.feed(rnd>>24); }
    assert(t.x<100 && t.y<30);
    for(unsigned scale=1;scale<=3;++scale) {
        assert(t.configure(scale,2,0)); t.reset();
        assert(t.cols==100/scale && t.rows==30/scale);
        put(t,"A\033[31mB\033[0mC");
        assert(t.configure(scale,6,4));
        assert(t.cells[0][0].fg==LcdTerminal::default_color && t.cells[0][1].fg==1);
        assert(t.cells[0][2].fg==LcdTerminal::default_color);
        assert(!t.configure(0,7,0) && !t.configure(4,7,0) && !t.configure(1,16,0) && !t.configure(1,2,2));
        assert(t.scale==scale && t.default_fg==6 && t.default_bg==4);
        for(unsigned i=0;i<200000;++i) {
            rnd=rnd*1664525u+1013904223u; t.feed(rnd>>24);
            assert(t.x<t.cols && t.y<t.rows);
        }
        t.reset();
        for(unsigned i=0;i<t.cols;++i) t.feed('a');
        t.feed('b'); assert(t.y==1 && t.cells[1][0].ch=='b');
    }
    // Fallback panel uses 6x8 cells: 160x128 gives a 26x16 terminal.
    assert(t.geometry(26,16));
    assert(t.configure(1,7,0)); t.reset();
    for(unsigned i=0;i<26;++i) t.feed('a');
    t.feed('b'); assert(t.y==1 && t.cells[1][0].ch=='b');
    put(t,"\033[999;999H"); assert(t.y==15 && t.x==25);
    put(t,"\r\nlast"); assert(t.y==15 && t.cells[15][0].ch=='l');
    assert(t.configure(2,7,0) && t.cols==13 && t.rows==8);
    assert(t.configure(3,7,0) && t.cols==8 && t.rows==5);
    assert(!t.geometry(0,16) && !t.geometry(101,16));
    assert(t.geometry(26,16));
    for(unsigned i=0;i<200000;++i) {
        rnd=rnd*1664525u+1013904223u; t.feed(rnd>>24);
        assert(t.x<t.cols && t.y<t.rows);
    }
    assert(t.geometry(1,1) && !t.configure(2,7,0));
    assert(t.cols==1 && t.rows==1);
    // Firmware allocates zeroed terminal storage rather than constructing it.
    LcdTerminal zero; memset(&zero,0,sizeof(zero));
    assert(zero.geometry(26,16) && zero.configure(1,7,0));
    assert(zero.cols==26 && zero.rows==16);
    puts("LCD ANSI terminal checks passed");
}
