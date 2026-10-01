#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace skdiag {
inline bool number(const char *s,uint32_t min,uint32_t max,uint32_t &out) {
    if(!s || !*s)return false;uint32_t n=0;
    for(;*s;++s){if(*s<'0' || *s>'9' || n>(max-unsigned(*s-'0'))/10 || unsigned(*s-'0')>max)return false;n=n*10+unsigned(*s-'0');}
    if(n<min || n>max)return false;out=n;return true;
}
inline bool ip(const char *s,uint32_t &out) {
    if(!s || strlen(s)>15)return false;char text[16];strcpy(text,s);out=0;
    char *part=text;
    for(unsigned i=0;i<4;++i){char *dot=strchr(part,'.');if((i<3)!=bool(dot))return false;if(dot)*dot=0;
        uint32_t n;if(!number(part,0,255,n))return false;out=(out<<8)|n;part=dot?dot+1:nullptr;}
    return true;
}
inline bool targets(const char *s,uint32_t &first,uint32_t &count) {
    if(!s || strlen(s)>40)return false;char text[41];strcpy(text,s);
    char *slash=strchr(text,'/'),*dash=strchr(text,'-');if(slash && dash)return false;
    if(slash){*slash++=0;uint32_t bits;if(!ip(text,first)||!number(slash,24,32,bits))return false;
        count=1U<<(32-bits);first&=~(count-1);if(bits<31){++first;count-=2;}return true;}
    if(dash){*dash++=0;uint32_t last;if(!ip(text,first)||!number(dash,0,255,last)||last<(first&255))return false;count=last-(first&255)+1;return true;}
    count=1;return ip(text,first);
}
inline bool ports(const char *s,uint16_t *out,size_t &count) {
    count=0;if(!s || !*s || strlen(s)>191)return false;char text[192];strcpy(text,s);char *part=text;
    while(part){char *comma=strchr(part,',');if(comma)*comma++=0;char *dash=strchr(part,'-');if(dash)*dash++=0;
        uint32_t lo,hi;if(!number(part,1,65535,lo))return false;hi=lo;if(dash && !number(dash,lo,65535,hi))return false;
        if(hi-lo+1>128)return false;
        for(uint32_t n=lo;n<=hi;++n){bool exists=false;for(size_t i=0;i<count;++i)exists|=out[i]==n;
            if(!exists){if(count==128)return false;out[count++]=n;}}
        part=comma;
    }return count>0;
}
inline uint32_t be32(const uint8_t *p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
inline bool stamp(const uint8_t *p,uint64_t &ms) {
    uint32_t seconds=be32(p),fraction=be32(p+4);if(!seconds && !fraction)return false;
    uint64_t expanded=seconds;if(expanded<3155673600ULL)expanded+=4294967296ULL;
    expanded-=2208988800ULL;if(expanded<946684800ULL || expanded>UINT32_MAX)return false;
    ms=expanded*1000+(uint64_t(fraction)*1000>>32);return true;
}
enum class NtpReply { Valid,Malformed,Unsynchronized,Denied,WrongRequest,Range };
inline NtpReply ntp(const uint8_t *p,size_t size,const uint8_t nonce[8],uint32_t rtt_ms,uint64_t &unix_ms) {
    if(size<48 || (p[0]&7)!=4 || ((p[0]>>3)&7)<3 || ((p[0]>>3)&7)>4)return NtpReply::Malformed;
    if(memcmp(p+24,nonce,8))return NtpReply::WrongRequest;
    if(!p[1])return NtpReply::Denied;
    if((p[0]>>6)==3 || p[1]>15)return NtpReply::Unsynchronized;
    uint64_t receive,transmit;
    if(!stamp(p+32,receive)||!stamp(p+40,transmit))return NtpReply::Range;
    if(transmit<receive || transmit-receive>uint64_t(rtt_ms)+1000)return NtpReply::Malformed;
    uint64_t processing=transmit-receive;
    unix_ms=transmit+(rtt_ms>processing?(rtt_ms-processing)/2:0);
    if(unix_ms/1000>UINT32_MAX)return NtpReply::Range;
    return NtpReply::Valid;
}
}
