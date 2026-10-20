#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Only the trace is rasterized: 540 bytes rather than a 40 KiB TFT buffer.
// Comparison with the prior raster lets the renderer replace pixels in place.
struct DmmTraceBitmap {
    enum { Width=156, Height=27, Stride=(Width+7)/8 };
    uint8_t pixels[Height][Stride]{};
    bool get(int x,int y) const { return (pixels[y][x/8] & (1U<<(x%8)))!=0; }
    void set(int x,int y) {
        if(x>=0 && x<Width && y>=0 && y<Height)pixels[y][x/8]|=1U<<(x%8);
    }
    void line(int x,int y,int end_x,int end_y) {
        const int dx=end_x>x?end_x-x:x-end_x, sx=x<end_x?1:-1;
        const int dy=-(end_y>y?end_y-y:y-end_y), sy=y<end_y?1:-1;
        int error=dx+dy;
        for(;;) {
            set(x,y);
            if(x==end_x && y==end_y)break;
            const int twice=2*error;
            if(twice>=dy) {error+=dy;x+=sx;}
            if(twice<=dx) {error+=dx;y+=sy;}
        }
    }
};

struct DmmPaintCache {
    bool valid=false,demo=false;
    uint32_t generation=0;
    char title[27]{},mode[27]{},number[8]{},extrema[27]{},footer[27]{};
    DmmTraceBitmap trace;
};

inline void dmm_paint_voltage(char (&text)[8],int32_t mv) {
    if(mv<0 || mv>999999) {strcpy(text,"OL");return;}
    const unsigned whole=unsigned(mv)/1000,frac=unsigned(mv)%1000;
    char *out=text;
    if(whole>=100)*out++=char('0'+whole/100);
    if(whole>=10)*out++=char('0'+whole/10%10);
    *out++=char('0'+whole%10);*out++='.';
    *out++=char('0'+frac/100);*out++=char('0'+frac/10%10);*out++=char('0'+frac%10);
    *out=0;
}

// Built-in font has an opaque foreground/background cell. Replacing a glyph
// directly avoids a visible erase-then-draw phase and clears shortened fields.
template<class Display,size_t N>
void dmm_paint_text(Display &display,unsigned x,unsigned y,unsigned scale,
                    uint16_t ink,uint16_t background,char (&before)[N],
                    const char *next,bool force=false) {
    const size_t length=strlen(next);
    display.setTextSize(scale);
    display.setTextColor(ink,background);
    for(size_t i=0;i<N-1;++i) {
        const char old=before[i]?before[i]:' ',ch=i<length?next[i]:' ';
        if(force || old!=ch) {
            display.setCursor(x+i*6*scale,y);
            display.write(uint8_t(ch));
        }
        before[i]=ch;
    }
    before[N-1]=0;
}
