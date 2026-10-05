#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <cstdio>
#define SK_NET_CHUNK 512
#define SK_NET_ENTROPY 1
#define CCM_CCGR_ON 3
#define CCM_CCGR6_TRNG(x) (x)
#define TRNG_MCTL_ERR (1U<<12)
static uint32_t CCM_CCGR6=3, TRNG_MCTL;
static unsigned initialized,recovered;
static size_t available;
namespace qindesign { namespace entropy {
void trng_init() { ++initialized;CCM_CCGR6=3;TRNG_MCTL=0;available=0; }
}}
static size_t qnethernet_hal_entropy_available() { return available; }
static size_t qnethernet_hal_fill_entropy(void *out,size_t size) {
    if(!size) {++recovered;TRNG_MCTL&=~TRNG_MCTL_ERR;available=0;return 0;}
    assert(available>=size);memset(out,0xa5,size);available-=size;return size;
}
struct Request { int length; };
struct Reply { int error=0,value=0;uint8_t data[SK_NET_CHUNK]={}; };
static Reply poll(int length) {
    Request q{length};Reply r;
    switch(SK_NET_ENTROPY) {
#include "entropy_case.inc"
    }
    return r;
}
int main() {
    // Boot may leave the clock enabled even though configuration is unknown.
    assert(poll(128).value==0 && initialized==1);
    // A completed sample stops the ring oscillator (TSTOP_OK) but is valid.
    TRNG_MCTL=(1U<<13)|(1U<<10);available=64;
    auto r=poll(128);assert(!r.error && r.value==64 && initialized==1);
    for(int i=0;i<r.value;++i)assert(r.data[i]==0xa5);
    assert(!poll(128).value && initialized==1);
    TRNG_MCTL|=TRNG_MCTL_ERR;available=64;r=poll(128);
    assert(!r.error && !r.value && recovered==1 && !available);
    available=64;assert(poll(16).value==16 && available==48);
    assert(poll(513).error==EINVAL);assert(poll(-1).error==EINVAL);
    CCM_CCGR6=0;assert(!poll(128).value && initialized==2);
    puts("PASS: entropy initialization, completed-sample preservation, error recovery, bounds and clock restart");
}
