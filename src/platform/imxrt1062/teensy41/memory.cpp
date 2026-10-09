#include <arduino_freertos.h>
#include <semphr.h>
#include "pool_accounting.h"
#include <malloc.h>
#include <limits.h>
#include "platform.h"
#include "memory_regions.h"
extern "C" {
#include "solar_os_memory.h"
extern uint8_t external_psram_size;
// The pinned FreeRTOS runtime overrides sbrk and maintains its own heap bounds.
// Arduino's __brkval is not updated by that allocator.
extern uint8_t *_g_heap_start, *_g_heap_max, *_g_current_heap_end;
extern unsigned long _estack;
extern uint8_t _extram_start[], _extram_end[];
extern uint8_t _heap_start[], _heap_end[];
}

static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t mutex;
static solar_os_memory_status_t statistics;
struct alignas(max_align_t) Header { size_t size; size_t external_charge; };
static size_t external_charged;
static size_t external_used, ocram_used;
static smalloc_pool ocram_pool;
// Conservative charge includes pinned smalloc's two metadata headers and
// rounding. Keep admission O(1): scanning all PSRAM on every allocation stalls
// consoles and audio. All external allocations in this port use this wrapper.
static constexpr size_t external_overhead = sizeof(Header) + 16*sizeof(size_t);

void sk_memory_begin() {
    // Arduino EXTMEM is NOLOAD; shared SolarOS EXT_RAM_BSS_ATTR means zeroed
    // static storage. Initialize it before any shared service or allocation.
    const size_t external_bss=uintptr_t(_extram_end)-uintptr_t(_extram_start);
    configASSERT(external_bss<=size_t(external_psram_size)*1024*1024);
    if(external_bss) memset(_extram_start,0,external_bss);
    // Never hand out memory overlapping the pinned core's MPU stack guard.
    configASSERT(reinterpret_cast<uintptr_t>(_g_heap_max) <=
                 reinterpret_cast<uintptr_t>(&_estack) - 8192);
    mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
    // FreeRTOS/newlib uses DTCM. The linker leaves the unused OCRAM tail
    // unclaimed; expose it without overlapping DMA/static buffers or newlib.
    const uintptr_t begin=(uintptr_t(_heap_start)+31U)&~uintptr_t(31U);
    const uintptr_t end=uintptr_t(_heap_end)&~uintptr_t(31U);
    configASSERT(uintptr_t(_g_heap_max)<=begin || uintptr_t(_g_heap_start)>=end);
    configASSERT(begin>=0x20200000U && end<=0x20280000U && end>begin);
    const int initialized=sm_set_pool(&ocram_pool,reinterpret_cast<void *>(begin),end-begin,1,nullptr);
    configASSERT(initialized);
    (void)initialized;
    statistics.internal_reserve = SOLAR_OS_MEMORY_INTERNAL_RESERVE_BYTES;
    statistics.internal_fallback_max = SOLAR_OS_MEMORY_INTERNAL_FALLBACK_MAX_BYTES;
}
static size_t dtcm_free() {
    const auto info = mallinfo();
    return (reinterpret_cast<uintptr_t>(_g_heap_max) -
            reinterpret_cast<uintptr_t>(_g_current_heap_end)) + info.fordblks;
}
static size_t ocram_free() {
    return ocram_pool.pool_size - ocram_used;
}
static bool is_ocram(const void *ptr) {
    const uintptr_t address=uintptr_t(ptr),begin=uintptr_t(ocram_pool.pool);
    return address>=begin && address<begin+ocram_pool.pool_size;
}
static size_t internal_free() { return dtcm_free()+ocram_free(); }
extern "C" void sk_memory_internal_regions(size_t *df,size_t *dt,size_t *of,size_t *ot) {
    xSemaphoreTake(mutex,portMAX_DELAY);
    *df=dtcm_free();*dt=uintptr_t(_g_heap_max)-uintptr_t(_g_heap_start);
    *of=ocram_free();*ot=ocram_pool.pool_size;
    xSemaphoreGive(mutex);
}
extern "C" bool solar_os_memory_is_external(const void *ptr) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    return address >= 0x70000000U &&
           address < 0x70000000U + size_t(external_psram_size) * 1024 * 1024;
}
extern "C" void *solar_os_memory_alloc(size_t size, solar_os_memory_class_t kind,
                                       const char *tag) {
    if (!mutex || unsigned(kind) >= SOLAR_OS_MEMORY_CLASS_COUNT ||
        !size || size > SIZE_MAX - external_overhead) return nullptr;
    xSemaphoreTake(mutex, portMAX_DELAY);
    auto &stats = statistics.classes[kind];
    ++stats.requests;
    stats.requested_bytes += size;
    Header *header = nullptr;
    const bool external = kind == SOLAR_OS_MEMORY_EXTERNAL_REQUIRED ||
        kind == SOLAR_OS_MEMORY_EXTERNAL_PREFERRED || kind == SOLAR_OS_MEMORY_TRANSIENT || kind == SOLAR_OS_MEMORY_EXTERNAL_SYSTEM;
    if (external && external_psram_size) {
        const size_t capacity=extmem_smalloc_pool.pool_size;
        const size_t available=external_charged<capacity ? capacity-external_charged : 0;
        const size_t reserve=kind==SOLAR_OS_MEMORY_EXTERNAL_SYSTEM ? 0 : 128U*1024U;
        if (available>=reserve && size+external_overhead<=available-reserve) {
            header=static_cast<Header *>(sk_pool_alloc(&extmem_smalloc_pool,size+sizeof(Header),external_used));
            if(header){header->external_charge=size+external_overhead;external_charged+=header->external_charge;}
        }
    }
    const bool may_fallback = !external || size <= statistics.internal_fallback_max;
    // Generic OCRAM allocations are CPU memory, not cache-coherent DMA buffers.
    if (!header && kind != SOLAR_OS_MEMORY_EXTERNAL_REQUIRED && kind != SOLAR_OS_MEMORY_EXTERNAL_SYSTEM &&
        kind != SOLAR_OS_MEMORY_DMA && may_fallback) {
        if (kind == SOLAR_OS_MEMORY_INTERNAL_CRITICAL) {
            header=static_cast<Header *>(malloc(size+sizeof(Header)));
        }
        if (!header) header=static_cast<Header *>(sk_pool_alloc(&ocram_pool,size+sizeof(Header),ocram_used));
        if (!header && kind != SOLAR_OS_MEMORY_INTERNAL_CRITICAL &&
            dtcm_free()>size+sizeof(Header)+statistics.internal_reserve)
            header=static_cast<Header *>(malloc(size+sizeof(Header)));
        if (header) {
            header->external_charge=0;
            if (external) ++stats.fallbacks;
        }
    }
    if (header) {
        header->size = size;
        ++stats.successes;
    } else {
        ++stats.failures;
        statistics.last_failure_valid = true;
        statistics.last_failure_class = kind;
        statistics.last_failure_size = size;
        strlcpy(statistics.last_failure_tag, tag ? tag : "", sizeof(statistics.last_failure_tag));
    }
    xSemaphoreGive(mutex);
    return header ? header + 1 : nullptr;
}
extern "C" void solar_os_memory_free(void *ptr) {
    if (!ptr) return;
    auto *header = static_cast<Header *>(ptr) - 1;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (solar_os_memory_is_external(header)) {external_charged-=header->external_charge;sk_pool_free(&extmem_smalloc_pool, header,external_used);}
    else if (is_ocram(header)) sk_pool_free(&ocram_pool,header,ocram_used);
    else free(header);
    xSemaphoreGive(mutex);
}
extern "C" void *solar_os_memory_calloc(size_t count, size_t size,
    solar_os_memory_class_t kind, const char *tag) {
    if (size && count > SIZE_MAX / size) return nullptr;
    void *ptr = solar_os_memory_alloc(count * size, kind, tag);
    if (ptr) memset(ptr, 0, count * size);
    return ptr;
}
extern "C" void *solar_os_memory_realloc(void *ptr, size_t size,
    solar_os_memory_class_t kind, const char *tag) {
    if (!size) { solar_os_memory_free(ptr); return nullptr; }
    void *next = solar_os_memory_alloc(size, kind, tag);
    if (next && ptr) {
        const size_t old_size = (static_cast<Header *>(ptr) - 1)->size;
        memcpy(next, ptr, old_size < size ? old_size : size);
        solar_os_memory_free(ptr);
    }
    return next;
}
extern "C" void solar_os_memory_get_status(solar_os_memory_status_t *status) {
    if (!status) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    statistics.dtcm.total = reinterpret_cast<uintptr_t>(_g_heap_max) -
                            reinterpret_cast<uintptr_t>(_g_heap_start);
    statistics.dtcm.free = dtcm_free();
    statistics.ocram.total = ocram_pool.pool_size;
    statistics.ocram.free = ocram_free();
    statistics.internal.total = statistics.dtcm.total + statistics.ocram.total;
    statistics.internal.free = statistics.dtcm.free + statistics.ocram.free;
    statistics.external.total = external_psram_size ? extmem_smalloc_pool.pool_size : 0;
    statistics.external.free = external_psram_size ? extmem_smalloc_pool.pool_size - external_used : 0;
    solar_os_memory_region_status_t *regions[] = {
        &statistics.dtcm, &statistics.ocram, &statistics.internal, &statistics.external
    };
    for (auto *region : regions) {
        if (!statistics.region_samples || region->free < region->minimum_free)
            region->minimum_free = region->free;
    }
    if (statistics.region_samples != UINT32_MAX) ++statistics.region_samples;
    // Largest contiguous blocks and DMA statistics are unavailable, left 0.
    *status = statistics;
    xSemaphoreGive(mutex);
}
extern "C" const char *solar_os_memory_class_name(solar_os_memory_class_t kind) {
    static const char *names[] = {"internal-critical", "internal-preferred", "dma",
        "external-required", "external-preferred", "transient", "external-system"};
    return unsigned(kind) < SOLAR_OS_MEMORY_CLASS_COUNT ? names[kind] : "invalid";
}
void sk_memory_print() {
    solar_os_memory_status_t status;
    solar_os_memory_get_status(&status);
    sk_console_printf("Internal heap: %u free / %u bytes; PSRAM: %u free / %u bytes\r\n",
        unsigned(status.internal.free), unsigned(status.internal.total),
        unsigned(status.external.free), unsigned(status.external.total));
    sk_console_printf("PSRAM ordinary-allocation reserve: 128 KiB; shell pipe budget: 64 KiB\r\n");
}
