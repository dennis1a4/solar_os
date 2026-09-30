#include "solar_os_telnet_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char wire_out[4096], plain[4096];
static size_t wn,pn;
static bool wire(void *u,const uint8_t *p,size_t n) { (void)u; assert(wn+n<=sizeof(wire_out)); memcpy(wire_out+wn,p,n); wn+=n; return true; }
static bool data(void *u,uint8_t b) { (void)u; assert(pn<sizeof(plain)); plain[pn++]=b; return true; }
static void feed(solar_telnet_codec_t *c,const uint8_t *p,size_t n) { for(size_t i=0;i<n;++i)assert(solar_telnet_feed(c,p[i])); }
int main(void) {
    solar_telnet_codec_t c; solar_telnet_codec_init(&c,wire,data,NULL);
    assert(solar_telnet_negotiate(&c) && wn==18);
    const uint8_t input[]={'a','\r','\n','b','\r',0,'c','\n',255,255,255,244};
    feed(&c,input,sizeof(input));
    const uint8_t expected[]={'a','\r','b','\r','c','\r',255,3};
    assert(pn==sizeof(expected) && !memcmp(plain,expected,pn));
    const uint8_t naws[]={255,250,31,0,100,0,30,255,240};
    feed(&c,naws,sizeof(naws)); assert(c.cols==100 && c.rows==30);
    const uint8_t escaped[]={255,250,31,0,255,255,0,24,255,240};
    feed(&c,escaped,sizeof(escaped)); assert(c.cols==255 && c.rows==24);
    const uint8_t invalid[]={255,250,31,0,1,0,1,255,240};
    feed(&c,invalid,sizeof(invalid)); assert(c.cols==255 && c.rows==24);
    const uint8_t begin[]={255,250,31}; feed(&c,begin,sizeof(begin));
    for(unsigned i=0;i<1000;++i)assert(solar_telnet_feed(&c,'z'));
    const uint8_t end[]={255,240}; feed(&c,end,sizeof(end)); assert(c.cols==255);
    const uint8_t terminal[]={255,250,24,0,'x','t','e','r','m',255,240};
    feed(&c,terminal,sizeof(terminal)); assert(!strcmp(c.terminal,"xterm"));
    wn=0; const uint8_t opts[]={255,253,99,255,251,99}; feed(&c,opts,sizeof(opts));
    const uint8_t refusal[]={255,252,99,255,254,99}; assert(wn==6 && !memcmp(wire_out,refusal,6));
    wn=0; const uint8_t output[]={'a','\r','\n',255,'\n'};
    for(unsigned i=0;i<sizeof(output);++i)assert(solar_telnet_write(&c,output+i,1));
    const uint8_t encoded[]={'a','\r',0,'\n',255,255,'\r','\n'};
    assert(wn==sizeof(encoded) && !memcmp(encoded,wire_out,wn));
    // Deterministic arbitrary bytes exercise recovery and bounds across all states.
    uint32_t seed=123;
    for(unsigned i=0;i<100000;++i) { wn=pn=0; seed=seed*1664525+1013904223; assert(solar_telnet_feed(&c,seed>>24)); }
    puts("telnet codec: streaming NVT, negotiation, NAWS, IAC, bounds passed");
}
