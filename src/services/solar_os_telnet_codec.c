#include "solar_os_telnet_codec.h"
#include <string.h>
enum { DATA, IAC, DO, DONT, WILL, WONT, SB_OPTION, SB_DATA, SB_IAC };
void solar_telnet_codec_init(solar_telnet_codec_t *c, bool (*wire)(void *,const uint8_t *,size_t), bool (*data)(void *,uint8_t),void *user) {
    memset(c,0,sizeof(*c)); c->wire=wire; c->data=data; c->user=user; c->cols=80; c->rows=24;
}
static bool command(solar_telnet_codec_t *c,uint8_t verb,uint8_t option) {
    uint8_t bytes[]={255,verb,option}; return c->wire(c->user,bytes,sizeof(bytes));
}
bool solar_telnet_negotiate(solar_telnet_codec_t *c) {
    const uint8_t bytes[]={255,251,1,255,251,3,255,253,3,255,253,24,255,253,31,255,254,34};
    return c->wire(c->user,bytes,sizeof(bytes));
}
static bool emit(solar_telnet_codec_t *c,uint8_t b) {
    if(c->suppress) { c->suppress=false; if(b=='\n' || b==0)return true; }
    if(b=='\r')c->suppress=true;
    else if(b=='\n')b='\r';
    return c->data(c->user,b);
}
static void sub(solar_telnet_codec_t *c,uint8_t b) {
    if(c->length<sizeof(c->sub))c->sub[c->length++]=b;
    else c->overflow=true;
}
bool solar_telnet_feed(solar_telnet_codec_t *c,uint8_t b) {
    switch(c->state) {
    case DATA: if(b==255)c->state=IAC; else return emit(c,b); break;
    case IAC:
        if(b==255) { c->state=DATA; return emit(c,b); }
        c->state=b==253?DO:b==254?DONT:b==251?WILL:b==252?WONT:b==250?SB_OPTION:DATA;
        if(b==244 || b==243)return c->data(c->user,3); // IP / BREAK -> Ctrl-C
        break;
    case DO: c->state=DATA; if(b!=1 && b!=3)return command(c,252,b); break;
    case WILL:
        c->state=DATA;
        if(b==24) { const uint8_t q[]={255,250,24,1,255,240}; return c->wire(c->user,q,sizeof(q)); }
        if(b!=3 && b!=31)return command(c,254,b);
        break;
    case DONT: case WONT: c->state=DATA; break;
    case SB_OPTION: c->option=b; c->length=0; c->overflow=false; c->state=SB_DATA; break;
    case SB_DATA: if(b==255)c->state=SB_IAC; else sub(c,b); break;
    case SB_IAC:
        if(b==255) { sub(c,b); c->state=SB_DATA; }
        else if(b==240) {
            if(!c->overflow && c->option==31 && c->length==4) {
                unsigned cols=(c->sub[0]<<8)|c->sub[1], rows=(c->sub[2]<<8)|c->sub[3];
                if(cols>=20 && cols<=300 && rows>=8 && rows<=120) { c->cols=cols; c->rows=rows; }
            } else if(!c->overflow && c->option==24 && c->length>1 && c->sub[0]==0) {
                size_t n=c->length-1; if(n>=sizeof(c->terminal))n=sizeof(c->terminal)-1;
                for(size_t i=0;i<n;++i) { unsigned ch=c->sub[i+1]; c->terminal[i]=ch>=32 && ch<127?ch:'?'; }
                c->terminal[n]=0;
            }
            c->state=DATA;
        } else { c->overflow=true; c->state=SB_DATA; } // discard malformed SB through SE
        break;
    default: c->state=DATA; break;
    }
    return true;
}
bool solar_telnet_write(solar_telnet_codec_t *c,const uint8_t *data,size_t n) {
    uint8_t out[256]; size_t used=0;
    for(size_t i=0;i<n;++i) {
        uint8_t b=data[i];
        if(used+2>sizeof(out)) { if(!c->wire(c->user,out,used))return false; used=0; }
        if(b==255) { out[used++]=255; out[used++]=255; }
        else if(b=='\r') { out[used++]='\r'; out[used++]=0; }
        else if(b=='\n') { if(!c->tx_cr)out[used++]='\r'; out[used++]='\n'; }
        else out[used++]=b;
        c->tx_cr=b=='\r';
    }
    return !used || c->wire(c->user,out,used);
}
