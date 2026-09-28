#include "lcd_terminal.h"
#include <cassert>
#include <cstring>
#include <cstdio>
static void put(LcdTerminal &t,const char *s) { t.write(s,strlen(s)); }
int main() {
    LcdTerminal t; t.reset();
    put(t,"abc\rZ"); assert(t.cells[0][0].ch=='Z' && t.cells[0][1].ch=='b');
    put(t,"\033[2;4Hhi\033[K"); assert(t.y==1 && t.x==5 && t.cells[1][3].ch=='h');
    put(t,"\033[s\033[30;1Hfooter\033[u"); assert(t.y==1 && t.x==5);
    put(t,"\033[1;29r\033[29;1Hbottom\r\nnext");
    assert(t.cells[27][0].ch=='b' && t.cells[28][0].ch=='n' && t.cells[29][0].ch=='f');
    put(t,"\033[2J\033[H\033[1;4;7;31mX");
    assert(t.cells[0][0].ch=='X' && t.cells[0][0].fg==1 && t.cells[0][0].flags==7);
    put(t,"\033[0m\033[?25l"); assert(!t.visible && !t.flags && t.fg==7);
    put(t,"\033[999999999;999999999H"); assert(t.y==29 && t.x==99);
    t.reset(); for(unsigned i=0;i<100;++i) t.feed('a');
    assert(t.y==0 && t.x==99); t.feed('b'); assert(t.y==1 && t.cells[1][0].ch=='b');
    put(t,"\033[2K"); for(auto c:t.cells[1]) assert(c.ch==' ');
    t.reset(); put(t,"\033]0;title\007OK"); assert(t.cells[0][0].ch=='O');
    put(t,"\xe2\x94\x80!"); assert(t.cells[0][2].ch=='?' && t.cells[0][3].ch=='!');
    // Malformed/random input must never index outside the terminal buffer.
    unsigned rnd=123456;
    for(unsigned i=0;i<200000;++i) { rnd=rnd*1664525u+1013904223u; t.feed(rnd>>24); }
    assert(t.x<100 && t.y<30);
    puts("LCD ANSI terminal checks passed");
}
