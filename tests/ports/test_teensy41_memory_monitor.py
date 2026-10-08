"""Exercise the actual status sampler with independent heaps and allocation failures."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MemoryMonitorTest(unittest.TestCase):
    def test_region_accounting_and_sampled_minima(self):
        source = (ROOT / 'src/platform/imxrt1062/teensy41/memory.cpp').read_text()
        start = source.index('extern "C" void solar_os_memory_get_status(')
        end = source.index('extern "C" const char *solar_os_memory_class_name', start)
        prefix = r'''
#include <cassert>
#include <cstdint>
extern "C" {
#include "solar_os_memory.h"
}
static solar_os_memory_status_t statistics{};
static uint8_t heap[80000];
static uint8_t *_g_heap_start=heap, *_g_heap_max=heap+sizeof(heap);
static struct { size_t pool_size; } ocram_pool{300000}, extmem_smalloc_pool{8200000};
static unsigned external_psram_size=8;
static size_t df=70000, of=280000, ef=8000000;
static int mutex, locks;
static const int portMAX_DELAY=0;
static void xSemaphoreTake(int,int){assert(!locks);++locks;}
static void xSemaphoreGive(int){assert(locks==1);--locks;}
static size_t dtcm_free(){return df;}
static size_t ocram_free(){return of;}
static void sm_malloc_stats_pool(void*,size_t *used,size_t *user,size_t *free,void*) {
    *used=0;*user=0;*free=ef;
}
'''
        checks = r'''
int main() {
    solar_os_memory_status_t s{};
    solar_os_memory_get_status(nullptr);assert(!locks);
    solar_os_memory_get_status(&s);
    assert(s.dtcm.total==80000 && s.ocram.total==300000);
    assert(s.external.total==8200000);
    assert(s.internal.total==380000 && s.internal.free==350000);
    assert(s.dtcm.minimum_free==70000 && s.external.minimum_free==8000000);
    assert(s.region_samples==1 && s.dtcm.largest_free==0);
    df=0;of=290000;ef=7900000;
    statistics.classes[0].failures=1;statistics.last_failure_valid=true;
    statistics.last_failure_size=1024;
    solar_os_memory_get_status(&s);
    assert(s.dtcm.minimum_free==0 && s.ocram.minimum_free==280000);
    assert(s.external.minimum_free==7900000 && s.internal.minimum_free==290000);
    assert(s.classes[0].failures==1 && s.last_failure_size==1024);
    df=70000;of=300000;ef=8000000;
    solar_os_memory_get_status(&s);
    assert(s.dtcm.minimum_free==0 && s.internal.free==370000);
    assert(s.external.minimum_free==7900000 && s.region_samples==3);
    statistics.region_samples=UINT32_MAX;
    solar_os_memory_get_status(&s);assert(s.region_samples==UINT32_MAX);
    statistics={};external_psram_size=0;
    solar_os_memory_get_status(&s);
    assert(s.external.total==0 && s.external.free==0 && s.external.minimum_free==0);
    assert(!locks);
}
'''
        with tempfile.TemporaryDirectory(prefix='teensy-memory-monitor-') as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(prefix + source[start:end] + checks)
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I'+str(ROOT/'src'), '-I'+str(ROOT/'src/services'),
                            str(path/'test.cpp'), '-o', str(path/'test')], check=True)
            subprocess.run([str(path/'test')], check=True)


if __name__ == '__main__':
    unittest.main()
