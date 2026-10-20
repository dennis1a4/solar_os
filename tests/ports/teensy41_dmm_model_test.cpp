#include "dmm_model.h"
#include <cassert>
#include <cstdio>

int main() {
    assert(dmm_scale(0,5000)==0 && dmm_scale(1023,5000)==5000);
    assert(dmm_scale(1023,50000)==50000 && dmm_scale(512,50000)==25024);
    assert(dmm_test_scale(1023)==13300);
    assert(dmm_test_scale(385)>4990 && dmm_test_scale(385)<5020);
    assert(dmm_test_scale(231)>2990 && dmm_test_scale(231)<3020);
    DmmModel bench;bench.test=true;bench.sample(0,3000);
    bench.set_range(50);assert(bench.test && bench.range==5 && bench.valid);
    bench.buttons(128);assert(bench.test && bench.range==5 && bench.valid);
    DmmModel m;
    m.toggle_hold();assert(!m.hold);
    m.sample(0,2000);m.sample(50,3000);m.sample(100,1000);
    assert(m.minimum==1000 && m.maximum==3000 && m.displayed()==1000);
    m.toggle_hold();m.sample(150,4000);
    assert(m.displayed()==1000 && m.live==4000 && m.maximum==4000 && m.count==4);
    m.toggle_hold();assert(m.displayed()==4000);
    m.set_mode(DmmModel::Minimum);assert(m.displayed()==1000);
    m.sample(200,5000,true);
    assert(!m.displayed_overload());
    m.set_mode(DmmModel::Maximum);assert(m.displayed()==5000 && m.displayed_overload());
    m.toggle_hold();m.sample(250,2500);
    assert(m.displayed()==5000 && m.displayed_overload());
    m.set_mode(DmmModel::Live);assert(!m.hold && !m.displayed_overload());
    m.reset_extrema();assert(m.minimum==2500 && m.maximum==2500 && !m.max_overload);
    m.set_range(50);assert(m.range==50 && !m.valid && !m.count && !m.hold);
    m.sample(300,25000);m.set_range(50);assert(m.valid && m.count==1);
    m.set_range(42);assert(m.range==50 && m.valid);
    assert(m.buttons(1) && m.hold);
    assert(m.buttons(2) && !m.hold && m.mode==DmmModel::Minimum);
    assert(m.buttons(16) && m.mode==DmmModel::Maximum);
    assert(m.buttons(16) && m.mode==DmmModel::Live);
    const unsigned saved_count=m.count;const int32_t saved_reading=m.live;
    int32_t low,high;
    assert(m.buttons(32) && !m.trace_auto && m.mode==DmmModel::Live);
    m.trace_limits(m.last_ms,low,high);assert(low==0 && high==50000);
    assert(m.count==saved_count && m.live==saved_reading);
    assert(m.buttons(32) && m.trace_auto && m.mode==DmmModel::Live);
    m.trace_limits(m.last_ms,low,high);assert(low==24990 && high==25010);
    m.trace_auto=false;m.set_range(5);m.trace_limits(0,low,high);
    assert(!m.trace_auto && low==0 && high==5000);
    assert(m.buttons(64) && m.range==5 && !m.valid);
    assert(m.buttons(128) && m.range==50 && !m.valid);
    assert(!m.buttons(8));
    // Capture remains live across all readout modes. Time spans ten seconds,
    // including uint32 millisecond rollover; a scheduling gap is observable.
    m=DmmModel{};
    const uint32_t base=UINT32_MAX-5000;
    for(unsigned i=0;i<250;++i)m.sample(base+i*50,int32_t(i));
    assert(m.count==201 && m.samples==250 && m.missed==0);
    assert(m.point(0).mv==49 && m.point(200).mv==249);
    assert(uint32_t(m.point(200).ms-m.point(0).ms)==10000);
    m.sample(m.last_ms+200,400);assert(m.missed==3);
    m.buttons(4);assert(m.minimum==400 && m.maximum==400);
    puts("DMM: ranges, holds, extrema/overload, NES actions, history/rollover and gaps passed");
}
