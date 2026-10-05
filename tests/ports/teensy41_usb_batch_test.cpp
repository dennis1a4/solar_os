#include <cassert>
#include <cstdio>
#include <vector>
#include "usb_sector_batch.h"
int main() {
    alignas(32) uint8_t scratch[4096];
    for(size_t sectors: {size_t(0),size_t(1),size_t(8),size_t(9),size_t(257)}) {
        size_t reads=0,seen=0;
        assert(sk_usb_read_batches(17,sectors,scratch,8,
            [&](uint32_t first,uint8_t *buf,size_t n) {
                assert(n && n<=8);++reads;
                for(size_t i=0;i<n;++i)memset(buf+i*512,(first+i)&255,512);
                return true;
            },[&](uint32_t first,uint8_t *buf,size_t n) {
                assert(first==17+seen);
                for(size_t i=0;i<n*512;++i)assert(buf[i]==uint8_t((first+i/512)&255));
                seen+=n;return true;
            }));
        assert(seen==sectors && reads==(sectors+7)/8);
    }
    unsigned calls=0,delivered=0;
    assert(!sk_usb_read_batches(0,24,scratch,8,[&](uint32_t,uint8_t *,size_t){return ++calls!=2;},
        [&](uint32_t,uint8_t *,size_t n){delivered+=n;return true;}));
    assert(calls==2 && delivered==8);
    calls=0;
    assert(!sk_usb_read_batches(UINT32_MAX,2,scratch,8,[&](uint32_t,uint8_t *,size_t){++calls;return true;},
        [](uint32_t,uint8_t *,size_t){return true;}));
    assert(!calls);
    assert(!sk_usb_read_batches(0,1,scratch,0,[](uint32_t,uint8_t *,size_t){return true;},
        [](uint32_t,uint8_t *,size_t){return true;}));
    calls=0;
    assert(!sk_usb_read_batches(0,24,scratch,8,[&](uint32_t,uint8_t *,size_t){++calls;return true;},
        [](uint32_t,uint8_t *,size_t){return false;}));assert(calls==1);
    puts("PASS: bounded USB batches, exact sector order/data, tail batches, read/callback failures and address overflow");
}
