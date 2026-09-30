#include "sd_recovery_state.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main() {
    SdRecoveryState s;
    assert(!s.io_allowed() && !s.acquire() && !s.release());
    assert(s.begin_mount());
    assert(s.io_allowed() && !s.mounted() && !s.acquire());
    s.finish_mount(false);
    assert(!s.io_allowed());
    assert(s.begin_mount());
    s.finish_mount(true);
    assert(s.mounted() && s.generation()==1);
    assert(s.acquire() && s.acquire());
    assert(!s.eject() && !s.begin_mount());
    const uint32_t old_generation=s.generation();
    s.removed();
    assert(!s.io_allowed() && s.generation()!=old_generation);
    // Replacement media must stay inaccessible until every old handle closes.
    s.allow_manual_mount();
    assert(!s.begin_mount() && !s.eject());
    assert(s.release() && !s.begin_mount());
    assert(s.release() && s.begin_mount());
    s.finish_mount(true);
    assert(s.mounted());
    assert(s.eject() && !s.io_allowed() && !s.begin_mount());
    s.allow_manual_mount();
    assert(s.begin_mount());
    // A transport failure during volume discovery overrides a late success.
    s.lost();
    s.finish_mount(true);
    assert(!s.mounted() && !s.io_allowed());
    s.removed();
    assert(s.begin_mount());
    s.finish_mount(true);
    for (unsigned cycle=0; cycle<10000; ++cycle) {
        unsigned handles=cycle%17;
        for (unsigned i=0; i<handles; ++i) assert(s.acquire());
        s.removed();
        assert(!s.io_allowed() && s.handles()==handles);
        while (handles) {
            assert(!s.begin_mount());
            assert(s.release()); --handles;
        }
        assert(!s.release());
        assert(s.begin_mount()); s.finish_mount(true);
        assert(s.mounted() && s.handles()==0);
        assert(s.eject() && !s.begin_mount());
        s.removed(); // removal releases the deliberate-eject latch
        assert(s.begin_mount()); s.finish_mount(true);
    }
    puts("PASS: SD lifecycle, stale handles, failed discovery, eject and 10000 replacement cycles");
}
