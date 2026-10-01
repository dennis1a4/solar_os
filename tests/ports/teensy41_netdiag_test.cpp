#include <cassert>
#include <cstdio>
#include <cstring>
#include "platform/imxrt1062/teensy41/net_diagnostics_protocol.h"
static void stamp(uint8_t *p,uint64_t unix_seconds,uint32_t fraction=0x80000000) {
    uint32_t n=uint32_t(unix_seconds+2208988800ULL);
    for(int i=0;i<4;++i){p[i]=uint8_t(n>>(24-8*i));p[4+i]=uint8_t(fraction>>(24-8*i));}
}
int main() {
    uint32_t first,count,n;uint16_t ports[128];size_t size;
    assert(skdiag::targets("192.168.1.90-100",first,count)&&count==11&&first==0xc0a8015a);
    assert(skdiag::targets("192.168.1.197/24",first,count)&&count==254&&first==0xc0a80101);
    assert(skdiag::targets("10.0.0.1/31",first,count)&&count==2&&first==0x0a000000);
    assert(skdiag::targets("10.0.0.1/32",first,count)&&count==1);
    for(auto *bad:{"1.2.3.256","1.2.3.4/23","1.2.3.4/33","1.2.3.4-3","1.2.3.4/24x","1.2.3.4-256","+1.2.3.4","1.2.3.4.5"})assert(!skdiag::targets(bad,first,count));
    assert(skdiag::ports("22,80,79-81,65535",ports,size)&&size==5);
    assert(skdiag::ports("1-128",ports,size)&&size==128);
    for(auto *bad:{"","0","-1","65536","1-129","80,","1,,2","22-21","1--2","42949672960"})assert(!skdiag::ports(bad,ports,size));
    assert(!skdiag::number("4294967296",0,UINT32_MAX,n));
    uint8_t nonce[8]={1,2,3,4,5,6,7,8},p[48]={};p[0]=0x24;p[1]=2;memcpy(p+24,nonce,8);
    stamp(p+32,1790000000);stamp(p+40,1790000000);uint64_t ms;
    using R=skdiag::NtpReply;
    assert(skdiag::ntp(p,48,nonce,100,ms)==R::Valid&&ms==1790000000550ULL);
    for(unsigned len=0;len<48;++len)assert(skdiag::ntp(p,len,nonce,100,ms)==R::Malformed);
    p[24]^=1;assert(skdiag::ntp(p,48,nonce,100,ms)==R::WrongRequest);p[24]^=1;
    p[1]=0;assert(skdiag::ntp(p,48,nonce,100,ms)==R::Denied);p[1]=16;assert(skdiag::ntp(p,48,nonce,100,ms)==R::Unsynchronized);p[1]=2;
    p[0]=0xe4;assert(skdiag::ntp(p,48,nonce,100,ms)==R::Unsynchronized);
    for(auto v:{0x23,0x14,0x2c}){p[0]=v;assert(skdiag::ntp(p,48,nonce,100,ms)==R::Malformed);}p[0]=0x24;
    stamp(p+32,2085978495);stamp(p+40,2085978496);assert(skdiag::ntp(p,48,nonce,1100,ms)==R::Valid&&ms==2085978496550ULL);
    stamp(p+32,UINT32_MAX);stamp(p+40,UINT32_MAX);assert(skdiag::ntp(p,48,nonce,100,ms)==R::Valid);
    stamp(p+40,uint64_t(UINT32_MAX)+1);assert(skdiag::ntp(p,48,nonce,100,ms)==R::Range);
    stamp(p+32,1790000001);stamp(p+40,1790000000);assert(skdiag::ntp(p,48,nonce,100,ms)==R::Malformed);
    stamp(p+32,1790000000);stamp(p+40,1790000002);assert(skdiag::ntp(p,48,nonce,100,ms)==R::Malformed);
    memset(p+40,0,8);assert(skdiag::ntp(p,48,nonce,100,ms)==R::Range);
    puts("PASS: bounded target/port parsing; NTP mode/version, nonce, synchronization, denial, timestamp order, 2036 rollover and RTC bounds");
}
