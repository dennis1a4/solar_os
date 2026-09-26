#include <arduino_freertos.h>
#include <semphr.h>
#include <smalloc.h>
#include <malloc.h>
#include <limits.h>
#include "platform.h"
extern "C" {
#include "solar_os_memory.h"
extern uint8_t external_psram_size;
// The pinned FreeRTOS runtime overrides sbrk and maintains its own heap bounds.
// Arduino's __brkval is not updated by that allocator.
extern uint8_t *_g_heap_start, *_g_heap_max, *_g_current_heap_end;
}

static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t mutex;
static solar_os_memory_status_t statistics;
struct alignas(max_align_t) Header { size_t size; };

void sk_memory_begin() {
    mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
    statistics.internal_reserve = SOLAR_OS_MEMORY_INTERNAL_RESERVE_BYTES;
    statistics.internal_fallback_max = SOLAR_OS_MEMORY_INTERNAL_FALLBACK_MAX_BYTES;
}
static size_t internal_free() {
    const auto info = mallinfo();
    return (reinterpret_cast<uintptr_t>(_g_heap_max) -
            reinterpret_cast<uintptr_t>(_g_current_heap_end)) + info.fordblks;
}
extern "C" bool solar_os_memory_is_external(const void *ptr) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    return address >= 0x70000000U &&
           address < 0x70000000U + size_t(external_psram_size) * 1024 * 1024;
}
extern "C" void *solar_os_memory_alloc(size_t size, solar_os_memory_class_t kind,
                                       const char *tag) {
    if (!mutex || unsigned(kind) >= SOLAR_OS_MEMORY_CLASS_COUNT ||
        !size || size > SIZE_MAX - sizeof(Header)) return nullptr;
    xSemaphoreTake(mutex, portMAX_DELAY);
    auto &stats = statistics.classes[kind];
    ++stats.requests;
    stats.requested_bytes += size;
    Header *header = nullptr;
    const bool external = kind == SOLAR_OS_MEMORY_EXTERNAL_REQUIRED ||
        kind == SOLAR_OS_MEMORY_EXTERNAL_PREFERRED || kind == SOLAR_OS_MEMORY_TRANSIENT;
    if (external && external_psram_size) {
        header = static_cast<Header *>(sm_malloc_pool(&extmem_smalloc_pool, size + sizeof(Header)));
    }
    const bool may_fallback = !external || size <= statistics.internal_fallback_max;
    // A generic malloc buffer is not a DMA allocation on cache-enabled M7.
    if (!header && kind != SOLAR_OS_MEMORY_EXTERNAL_REQUIRED &&
        kind != SOLAR_OS_MEMORY_DMA && may_fallback &&
        internal_free() > size + sizeof(Header) + statistics.internal_reserve) {
        header = static_cast<Header *>(malloc(size + sizeof(Header)));
        if (header && external) ++stats.fallbacks;
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
    if (solar_os_memory_is_external(header)) sm_free_pool(&extmem_smalloc_pool, header);
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
    statistics.internal.total = reinterpret_cast<uintptr_t>(_g_heap_max) -
                                reinterpret_cast<uintptr_t>(_g_heap_start);
    statistics.internal.free = internal_free();
    statistics.external.total = size_t(external_psram_size) * 1024 * 1024;
    if (external_psram_size) {
        size_t used = 0, user = 0, available = 0;
        sm_malloc_stats_pool(&extmem_smalloc_pool, &used, &user, &available, nullptr);
        statistics.external.free = available;
    }
    // largest_free/minimum_free and DMA statistics are unavailable, left 0.
    *status = statistics;
    xSemaphoreGive(mutex);
}
extern "C" const char *solar_os_memory_class_name(solar_os_memory_class_t kind) {
    static const char *names[] = {"internal-critical", "internal-preferred", "dma",
        "external-required", "external-preferred", "transient"};
    return unsigned(kind) < SOLAR_OS_MEMORY_CLASS_COUNT ? names[kind] : "invalid";
}
void sk_memory_print() {
    solar_os_memory_status_t status;
    solar_os_memory_get_status(&status);
    sk_console_printf("Internal heap: %u free / %u bytes; PSRAM: %u free / %u bytes\r\n",
        unsigned(status.internal.free), unsigned(status.internal.total),
        unsigned(status.external.free), unsigned(status.external.total));
}
