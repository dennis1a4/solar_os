#pragma once
#include <stdint.h>
#include <stddef.h>

// Fixed-cell ANSI terminal for the RA8875 internal 8x16 font. Independent of
// Arduino so cursor/erase/scroll handling can be checked on the host.
struct LcdTerminal {
    static constexpr unsigned cols=100, rows=30;
    struct Cell { uint8_t ch, fg, bg, flags; };
    Cell cells[rows][cols];
    bool dirty[rows][cols];
    unsigned x=0,y=0,sx=0,sy=0,top=0,bottom=rows-1;
    uint8_t fg=7,bg=0,flags=0,state=0;
    unsigned params[16]{}, count=0;
    bool visible=true,wrap=false,autowrap=true,priv=false,overflow=false;
    uint8_t utf8=0;
    void reset();
    void feed(uint8_t ch);
    void write(const char *text,size_t len);
private:
    void blank(unsigned row,unsigned first,unsigned end);
    void scroll(bool reverse=false);
    void newline();
    void csi(uint8_t ch);
};
