#pragma once
#include <stdint.h>
#include <string.h>

// Dashboard rows use the built-in 6-pixel-wide font, at most 26 columns.
// Cache only completed writes; callers retain SPI ownership during draw().
struct SmallMonitorRow {
    char text[27]{};
    uint16_t color{};
    bool valid{};

    bool matches(const char *next, uint16_t ink) const {
        return valid && color==ink && !strcmp(text,next);
    }

    template<class Display>
    void draw(Display &display, unsigned row, const char *next,
              uint16_t ink, uint16_t background) {
        if(matches(next,ink)) return;
        const unsigned length=strlen(next), old_length=strlen(text);
        unsigned first=0, end=length>old_length?length:old_length;
        const bool full=!valid || color!=ink;
        if(!full) {
            while(first<length && first<old_length && text[first]==next[first]) ++first;
            while(end>first && end<=length && end<=old_length && text[end-1]==next[end-1]) --end;
        }
        const unsigned y=row*14;
        display.fillRect(first*6,y,full?display.width():(end-first)*6,12,background);
        display.setTextColor(ink);
        display.setCursor(first*6,y+2);
        for(unsigned i=first;i<end && i<length;++i) display.write(next[i]);
        strcpy(text,next);
        color=ink;
        valid=true;
    }
};
