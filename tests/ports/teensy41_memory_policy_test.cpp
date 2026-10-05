#include <sys/mman.h>
#include <vector>
#include <cstdio>
#include <algorithm>
#include "../../src/platform/imxrt1062/teensy41/memory.cpp"
extern "C" {
uint8_t external_psram_size=8;
uint8_t *_g_heap_start=nullptr,*_g_heap_max=nullptr,*_g_current_heap_end=nullptr;
unsigned long _estack;
uint8_t _extram_start[1],_extram_end[1];
}
smalloc_pool extmem_smalloc_pool;
static size_t live_bytes;
static bool pool_failure;
struct Allocation {void *ptr;size_t bytes;smalloc_pool *pool;};
static std::vector<Allocation> blocks;
void *sm_malloc_pool(smalloc_pool *p,size_t n) {
    if(pool_failure)return nullptr;
    if(!p->pool)return nullptr;
    size_t bump=0;
    for(const auto &a:blocks)if(a.pool==p)bump=std::max(bump,size_t(static_cast<char *>(a.ptr)-static_cast<char *>(p->pool))+a.bytes);
    n=(n+31)&~size_t(31);
    if(bump+n>p->pool_size)return nullptr;
    void *out=static_cast<char *>(p->pool)+bump;bump+=n;live_bytes+=n;blocks.push_back({out,n,p});return out;
}
void sm_free_pool(smalloc_pool *,void *ptr) {
    auto it=std::find_if(blocks.begin(),blocks.end(),[ptr](const Allocation &a){return a.ptr==ptr;});
    assert(it!=blocks.end());live_bytes-=it->bytes;blocks.erase(it);
}
int sm_malloc_stats_pool(smalloc_pool *p,size_t *used,size_t *user,size_t *available,int *) {
    assert(used && available); // Required by the pinned smalloc stats implementation.
    size_t bytes=0;for(const auto &a:blocks)if(a.pool==p)bytes+=a.bytes;
    if(used)*used=bytes;if(user)*user=bytes;if(available)*available=p->pool_size-bytes;return 0;
}
int main() {
    void *arena=mmap(reinterpret_cast<void *>(0x70000000),8*1024*1024,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(arena!=MAP_FAILED);extmem_smalloc_pool={arena,8*1024*1024};mutex=&mutex_storage;
    // Fill the ordinary budget, then demonstrate that settings and pipes can
    // still allocate from the reserve without falling back to internal RAM.
    const size_t ordinary=extmem_smalloc_pool.pool_size-128*1024-external_overhead;
    void *app=solar_os_memory_alloc(ordinary,SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"app");assert(app);
    assert(!solar_os_memory_alloc(1,SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"full"));
    void *system=solar_os_memory_alloc(8192,SOLAR_OS_MEMORY_EXTERNAL_SYSTEM,"pipe");assert(system);
    solar_os_memory_free(system);solar_os_memory_free(app);assert(external_charged==0 && blocks.empty());
    pool_failure=true;
    assert(!solar_os_memory_alloc(8192,SOLAR_OS_MEMORY_EXTERNAL_SYSTEM,"fragmented"));
    assert(external_charged==0);pool_failure=false;
    for(int i=0;i<100;++i){
        app=solar_os_memory_calloc(32,16,SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"app");assert(app);
        for(size_t j=0;j<512;++j)assert(static_cast<char *>(app)[j]==0);
        memset(app,0x5a,512);
        void *next=solar_os_memory_realloc(app,1024,SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"grow");assert(next);
        for(size_t j=0;j<512;++j)assert(static_cast<unsigned char *>(next)[j]==0x5a);
        pool_failure=true;assert(!solar_os_memory_realloc(next,2048,SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"fail"));pool_failure=false;
        solar_os_memory_free(next);assert(external_charged==0 && blocks.empty());
    }
    assert(!solar_os_memory_alloc(SIZE_MAX,SOLAR_OS_MEMORY_EXTERNAL_SYSTEM,"overflow"));
    assert(!solar_os_memory_calloc(SIZE_MAX,2,SOLAR_OS_MEMORY_EXTERNAL_SYSTEM,"overflow"));
    void *internal=mmap(reinterpret_cast<void *>(0x20200000),256*1024,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(internal!=MAP_FAILED);ocram_pool={internal,256*1024};
    const size_t before=ocram_free();
    for(int i=0;i<100;++i) {
        void *stack=solar_os_memory_alloc(40960,SOLAR_OS_MEMORY_INTERNAL_PREFERRED,"telnet.stack");
        assert(stack && is_ocram(stack) && !solar_os_memory_is_external(stack));
        assert(uintptr_t(stack)%alignof(max_align_t)==0);
        memset(stack,0xa5,40960);
        void *worker=solar_os_memory_calloc(1,28672,SOLAR_OS_MEMORY_INTERNAL_PREFERRED,"worker.stack");
        assert(worker && is_ocram(worker));
        assert(static_cast<unsigned char *>(worker)[28671]==0);
        assert(ocram_free()<before-40960-28672);
        void *grown=solar_os_memory_realloc(stack,45056,SOLAR_OS_MEMORY_INTERNAL_PREFERRED,"grow");
        assert(grown && is_ocram(grown));
        for(size_t j=0;j<40960;++j)assert(static_cast<unsigned char *>(grown)[j]==0xa5);
        solar_os_memory_free(grown);solar_os_memory_free(worker);
        assert(ocram_free()==before && external_charged==0 && blocks.empty());
    }
    assert(!solar_os_memory_alloc(64,SOLAR_OS_MEMORY_DMA,"no-dma"));
    munmap(internal,256*1024);
    munmap(arena,8*1024*1024);
    puts("PASS: PSRAM reserve, OCRAM stack routing/alignment/realloc, DMA refusal and exact recovery");
}
