#include "lcd_terminal.h"
#include <string.h>
#include <algorithm>

void LcdTerminal::blank(unsigned row,unsigned first,unsigned end) {
    for(unsigned col=first;col<end;++col) {
        cells[row][col]={' ',fg,bg,flags}; dirty[row][col]=true;
    }
}
void LcdTerminal::reset() {
    x=y=sx=sy=top=0; bottom=rows-1; fg=7; bg=flags=state=utf8=0;
    visible=autowrap=true; wrap=priv=overflow=false; count=0;
    for(unsigned row=0;row<rows;++row) blank(row,0,cols);
}
void LcdTerminal::scroll(bool reverse) {
    if(reverse) {
        for(unsigned row=bottom;row>top;--row) memcpy(cells[row],cells[row-1],sizeof(cells[row]));
        blank(top,0,cols);
    } else {
        for(unsigned row=top;row<bottom;++row) memcpy(cells[row],cells[row+1],sizeof(cells[row]));
        blank(bottom,0,cols);
    }
    for(unsigned row=top;row<=bottom;++row) memset(dirty[row],true,sizeof(dirty[row]));
}
void LcdTerminal::newline() {
    if(y==bottom) scroll(); else if(y<rows-1) ++y;
    wrap=false;
}
void LcdTerminal::csi(uint8_t ch) {
    const unsigned n=params[0]?params[0]:1;
    switch(ch) {
    case 'A': y-=std::min(y,n); break;
    case 'B': y=std::min(rows-1,y+n); break;
    case 'C': x=std::min(cols-1,x+n); break;
    case 'D': x-=std::min(x,n); break;
    case 'G': x=std::min(cols-1,n-1); break;
    case 'd': y=std::min(rows-1,n-1); break;
    case 'H': case 'f': y=std::min(rows-1,n-1); x=std::min(cols-1,params[1]?params[1]-1:0); break;
    case 'J':
        if(params[0]==2 || params[0]==3) for(unsigned r=0;r<rows;++r) blank(r,0,cols);
        else if(params[0]==0) { blank(y,x,cols); for(unsigned r=y+1;r<rows;++r) blank(r,0,cols); }
        else if(params[0]==1) { for(unsigned r=0;r<y;++r) blank(r,0,cols); blank(y,0,x+1); }
        break;
    case 'K': blank(y,params[0]==0?x:0,params[0]==1?x+1:cols); break;
    case 's': sx=x; sy=y; break;
    case 'u': x=sx; y=sy; break;
    case 'r': {
        unsigned a=n-1,b=params[1]?params[1]-1:rows-1;
        if(a<b && b<rows) { top=a; bottom=b; x=y=0; }
        break;
    }
    case 'h': case 'l':
        if(priv) for(unsigned i=0;i<=count;++i) {
            if(params[i]==25) visible=ch=='h';
            if(params[i]==7) autowrap=ch=='h';
        }
        break;
    case 'm':
        for(unsigned i=0;i<=count;++i) {
            unsigned p=params[i];
            if(p==0) { fg=7; bg=flags=0; }
            else if(p==1) flags|=1;
            else if(p==4) flags|=2;
            else if(p==7) flags|=4;
            else if(p==22) flags&=~1;
            else if(p==24) flags&=~2;
            else if(p==27) flags&=~4;
            else if(p>=30 && p<=37) fg=p-30;
            else if(p>=40 && p<=47) bg=p-40;
            else if(p>=90 && p<=97) fg=p-90+8;
            else if(p>=100 && p<=107) bg=p-100+8;
            else if(p==39) fg=7;
            else if(p==49) bg=0;
        }
        break;
    default: break;
    }
    wrap=false;
}
void LcdTerminal::feed(uint8_t ch) {
    dirty[y][x]=true;
    if(state==3) { if(ch==7) state=0; else if(ch==27) state=4; return; }
    if(state==4) { state=ch=='\\'?0:3; return; }
    if(ch==27) { state=1; utf8=0; return; }
    if(state==1) {
        state=0;
        if(ch=='[') { state=2; memset(params,0,sizeof(params)); count=0; priv=overflow=false; }
        else if(ch==']') state=3;
        else if(ch=='7') { sx=x; sy=y; }
        else if(ch=='8') { x=sx; y=sy; }
        else if(ch=='D') newline();
        else if(ch=='M') { if(y==top) scroll(true); else if(y) --y; }
        else if(ch=='c') reset();
    } else if(state==2) {
        if(ch=='?') priv=true;
        else if(ch>='0' && ch<='9') params[count]=std::min(10000u,params[count]*10+ch-'0');
        else if(ch==';') { if(count<15) ++count; else overflow=true; }
        else if(ch>=0x40 && ch<=0x7e) { if(!overflow) csi(ch); state=0; }
    } else if(ch=='\r') { x=0; wrap=false; }
    else if(ch=='\n') newline();
    else if(ch=='\b') { if(x) --x; wrap=false; }
    else if(ch=='\t') { x=std::min(cols-1,(x/8+1)*8); wrap=false; }
    else if(ch>=32 && ch!=127) {
        // Local shell advertises ASCII. Collapse unsupported UTF-8 glyphs into
        // one replacement cell instead of rendering continuation-byte garbage.
        if((ch&0xc0)==0x80 && utf8) { --utf8; return; }
        utf8=ch>=0xf0?3:ch>=0xe0?2:ch>=0xc0?1:0;
        if(ch>=128) ch='?';
        if(wrap && autowrap) { x=0; newline(); }
        cells[y][x]={ch,fg,bg,flags}; dirty[y][x]=true;
        if(x<cols-1) ++x; else wrap=autowrap;
    }
    dirty[y][x]=true;
}
void LcdTerminal::write(const char *text,size_t len) { while(len--) feed(uint8_t(*text++)); }
