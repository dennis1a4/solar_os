#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
#include "graphics_presenter.h"

int main() {
    std::vector<uint8_t> data(808 * 480), dirty(14 * 60, 255);
    std::vector<uint32_t> hashes(101 * 60);
    std::vector<uint16_t> panel(800 * 480, 0xffff);
    uint16_t palette[256]{};
    for (unsigned i = 0; i < 256; ++i) palette[i] = i * 127;
    solar_os_display_surface_t s{};
    s.data=data.data(); s.data_size=data.size(); s.palette_rgb565=palette; s.palette_size=256;
    s.dirty_tiles=dirty.data(); s.dirty_size=dirty.size(); s.dirty_stride=14;
    s.presented_hashes=hashes.data(); s.presented_hash_count=hashes.size(); s.hash_stride=101;
    s.width=s.native_width=800; s.height=s.native_height=480; s.stride=808;
    s.tile_size=8; s.format=SOLAR_OS_DISPLAY_FORMAT_INDEX8;
    SkGraphicsPresenter presenter;
    unsigned sent=0, yields=0;
    auto write=[&](unsigned x,unsigned y,unsigned w,const uint16_t *p) {
        assert(x+w<=800 && y<480); sent+=w;
        for(unsigned i=0;i<w;++i) panel[y*800+x+i]=p[i];
        return true;
    };
    auto present=[&] { sent=0; assert(presenter.present(&s,write,[&]{++yields;})); };
    auto check=[&] {
        for(unsigned y=0;y<480;++y) for(unsigned x=0;x<800;++x)
            assert(panel[y*800+x]==palette[data[y*808+x]]);
    };
    present(); assert(sent==384000 && yields==15); check();
    present(); assert(sent==0); // Full clear/redraw with identical content.
    data[479*808+799]=5; present(); assert(sent==64); check();
    std::fill(dirty.begin(),dirty.end(),0);
    palette[5]=0x1234; present(); assert(sent==384000); check(); // Clean map, changed palette.
    hashes[59*101+99]=0; present(); assert(sent==64); // Snapshot invalidation.
    presenter.reset(); present(); assert(sent==384000); // Re-entry at same address.
    s.presented_hashes=nullptr; std::fill(dirty.begin(),dirty.end(),255);
    present(); assert(sent==384000); check(); s.presented_hashes=hashes.data();
    // Random dirty rectangles compared with a full-frame reference.
    uint32_t rng=42;
    for(unsigned frame=0;frame<50;++frame) {
        std::fill(dirty.begin(),dirty.end(),0);
        for(unsigned i=0;i<30;++i) {
            rng=rng*1664525+1013904223; unsigned x=rng%800;
            rng=rng*1664525+1013904223; unsigned y=rng%480;
            data[y*808+x]=rng>>24; dirty[(y/8)*14+(x/8)/8]|=1U<<((x/8)%8);
        }
        present(); check();
    }
    presenter.reset(); unsigned calls=0;
    assert(!presenter.present(&s,[&](unsigned,unsigned,unsigned,const uint16_t*){return ++calls<12;},[]{}));
    present(); assert(sent==384000); check(); // Retry after partial transport failure.
    auto reject=[&](solar_os_display_surface_t bad) {
        sent=0; assert(!presenter.present(&bad,write,[]{})); assert(sent==0);
    };
    auto bad=s; bad.data=nullptr; reject(bad);
    bad=s; bad.palette_size=255; reject(bad);
    bad=s; bad.dirty_stride=12; reject(bad);
    bad=s; bad.dirty_size=1; reject(bad);
    bad=s; bad.hash_stride=99; reject(bad);
    bad=s; bad.presented_hash_count=1; reject(bad);
    bad=s; bad.data_size=1; reject(bad);
    bad=s; bad.rotation=SOLAR_OS_DISPLAY_ROTATION_90; reject(bad);
    bad=s; bad.native_width=480; reject(bad);
    bad=s; bad.tile_size=16; reject(bad);
    puts("PASS: differential presentation, palette, strides, bounds, reset and transport retry");
}
