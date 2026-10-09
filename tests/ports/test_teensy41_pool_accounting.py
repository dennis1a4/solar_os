"""Compare incremental accounting against the pinned allocator's full scan."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
core = Path.home() / '.platformio/packages/framework-arduinoteensy/cores/teensy4'
source = r'''
#include <cassert>
#include "pool_accounting.h"
int main() {
    alignas(max_align_t) unsigned char bytes[32768]{};
    smalloc_pool pool{};
    assert(sm_set_pool(&pool,bytes,sizeof(bytes),1,nullptr));
    size_t tracked=0;
    void *blocks[64]{};
    for(unsigned step=0;step<400;++step) {
        unsigned slot=(step*17)%64;
        if(blocks[slot]) { sk_pool_free(&pool,blocks[slot],tracked); blocks[slot]=nullptr; }
        else blocks[slot]=sk_pool_alloc(&pool,1+(step*37)%1100,tracked);
        size_t used=0,user=0,free=0;
        sm_malloc_stats_pool(&pool,&used,&user,&free,nullptr);
        assert(used==tracked && free==pool.pool_size-tracked);
    }
    const size_t before=tracked;
    assert(!sk_pool_alloc(&pool,sizeof(bytes)*2,tracked));
    assert(tracked==before);
    for(auto block:blocks) sk_pool_free(&pool,block,tracked);
    assert(tracked==0);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp)
    objects=[]
    for file in core.glob('sm_*.c'):
        obj=tmp/(file.stem+'.o')
        subprocess.run(['cc','-I'+str(core),'-c',str(file),'-o',str(obj)],check=True)
        objects.append(str(obj))
    test=tmp/'test.cpp';test.write_text(source)
    binary=tmp/'test'
    subprocess.run(['c++','-std=c++17','-I'+str(core),'-I'+str(root/'src/platform/imxrt1062/teensy41'),str(test),*objects,'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS: incremental pool accounting matches allocator scans through fragmentation, failures and recovery')
