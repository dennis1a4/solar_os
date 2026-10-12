#include "small_monitor_row.h"
#include <cassert>
#include <cstdio>
#include <vector>

struct Display {
    struct Pixel { unsigned value=0, ink=0; };
    std::vector<Pixel> pixels=std::vector<Pixel>(160*128);
    unsigned x=0,y=0,ink=0,cleared=0,written=0;
    unsigned width() const { return 160; }
    void fillRect(unsigned left,unsigned top,unsigned w,unsigned h,unsigned bg) {
        assert(left+w<=160 && top+h<=128);
        cleared+=w*h;
        for(unsigned b=top;b<top+h;++b) for(unsigned a=left;a<left+w;++a)
            pixels[b*160+a]={0,bg};
    }
    void setTextColor(unsigned c) { ink=c; }
    void setCursor(unsigned a,unsigned b) { x=a;y=b; }
    void write(char c) {
        ++written;
        // Model transparent glyphs, including the untouched inter-character gap.
        for(unsigned b=y;b<y+8;++b) for(unsigned a=x;a<x+5;++a)
            if(c!=' ') pixels[b*160+a]={unsigned(c),ink};
        x+=6;
    }
};

int main() {
    SmallMonitorRow cache;
    Display actual,reference;
    auto check=[&](const char *text,unsigned ink,unsigned row=0,unsigned bg=0) {
        cache.draw(actual,row,text,ink,bg);
        reference.fillRect(0,row*14,160,12,bg);
        reference.setTextColor(ink); reference.setCursor(0,row*14+2);
        for(const char *p=text;*p;++p) reference.write(*p);
        for(unsigned i=0;i<actual.pixels.size();++i) {
            assert(actual.pixels[i].value==reference.pixels[i].value);
            assert(actual.pixels[i].ink==reference.pixels[i].ink);
        }
        assert(cache.matches(text,ink));
    };
    check(" 4:30:09 pm  2026-10-10",1);
    unsigned before=actual.cleared, chars=actual.written;
    check(" 4:30:10 pm  2026-10-10",1);
    assert(actual.cleared-before==12*12 && actual.written-chars==2);
    before=actual.cleared;chars=actual.written;
    check(" 4:30:10 pm  2026-10-10",1);
    assert(actual.cleared==before && actual.written==chars);
    check("12:00:00 am  2026-10-11",1);
    check("CPU 10.0%",1); check("CPU 9.9%",1); check("CPU 100.0%",1);
    check("DTCM  7/74 KiB",2);
    before=actual.cleared;
    check("DTCM  7/74 KiB",3); // Color-only alarm transitions must repaint.
    assert(actual.cleared-before==160*12);
    check("DTCM  7/74 KiB",2);
    check("",2); check("",3);
    check("12345678901234567890123456",3); check("short",3); check("",3);
    cache.valid=false; // Ownership release/page invalidation must force redraw.
    before=actual.cleared; check("",3);
    assert(actual.cleared-before==160*12);
    cache.valid=false;
    check("< > Page  Select Home",1,8,4);
    check("< > Page  A Play/Pause",1,8,4);
    puts("PASS: dashboard partial redraw matches full redraw; unchanged rows, alarm colors, erasure and invalidation");
}
