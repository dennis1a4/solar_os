#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// The supplied scratch buffer is internal, DMA-aligned RAM. Deliver callbacks
// in task context only after an entire USB read completes successfully.
template<class Read, class Deliver>
bool sk_usb_read_batches(uint32_t sector, size_t count, uint8_t *scratch,
                         size_t batch_sectors, Read read, Deliver deliver) {
    if (!batch_sectors || (count && count - 1 > UINT32_MAX - sector)) return false;
    while (count) {
        const size_t n=count<batch_sectors?count:batch_sectors;
        if (!read(sector,scratch,n)) return false;
        if (!deliver(sector,scratch,n)) return false;
        sector+=n;count-=n;
    }
    return true;
}
