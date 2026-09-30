#pragma once
#include <string.h>
#include "solar_os_display_surface.h"

// Fixed landscape panel adapter. Zero hashes mean never presented.
class SkGraphicsPresenter {
    uint16_t palette[256]{};
    uint16_t row[800];
    const uint8_t *pixels = nullptr;
    bool valid = false;
public:
    void reset() { valid = false; }

    template<class Write, class Yield>
    bool present(const solar_os_display_surface_t *s, Write write, Yield yield) {
        if (!s || !s->data || !s->palette_rgb565 || s->palette_size < 256 ||
            s->format != SOLAR_OS_DISPLAY_FORMAT_INDEX8 ||
            s->rotation != SOLAR_OS_DISPLAY_ROTATION_0 ||
            s->width != 800 || s->height != 480 ||
            s->native_width != 800 || s->native_height != 480 ||
            s->stride < 800 || s->data_size < size_t(s->stride) * 480 ||
            s->tile_size != 8 ||
            (s->dirty_tiles && (s->dirty_stride < 13 ||
                s->dirty_size < size_t(s->dirty_stride) * 60)) ||
            (s->presented_hashes && (s->hash_stride < 100 ||
                s->presented_hash_count < size_t(s->hash_stride) * 60)))
            return false;

        const bool force = !valid || pixels != s->data ||
            memcmp(palette, s->palette_rgb565, sizeof(palette));
        for (unsigned ty = 0; ty < 60; ++ty) {
            bool changed[100]{};
            uint32_t hashes[100]{};
            for (unsigned tx = 0; tx < 100; ++tx) {
                const size_t at = size_t(ty) * s->hash_stride + tx;
                const bool unknown = s->presented_hashes && !s->presented_hashes[at];
                if (!force && !unknown && s->dirty_tiles &&
                    !(s->dirty_tiles[ty * s->dirty_stride + tx / 8] & (1U << (tx % 8))))
                    continue;
                uint32_t hash = 2166136261U;
                for (unsigned y = ty * 8; y < ty * 8 + 8; ++y)
                    for (unsigned x = tx * 8; x < tx * 8 + 8; ++x)
                        hash = (hash ^ s->data[y * s->stride + x]) * 16777619U;
                if (!hash) hash = 1;
                hashes[tx] = hash;
                changed[tx] = force || !s->presented_hashes || s->presented_hashes[at] != hash;
            }
            for (unsigned y = ty * 8; y < ty * 8 + 8; ++y) {
                for (unsigned tx = 0; tx < 100;) {
                    if (!changed[tx]) { ++tx; continue; }
                    unsigned end = tx + 1;
                    while (end < 100 && changed[end]) ++end;
                    for (unsigned x = tx * 8; x < end * 8; ++x)
                        row[x] = s->palette_rgb565[s->data[y * s->stride + x]];
                    if (!write(tx * 8, y, (end - tx) * 8, row + tx * 8)) {
                        reset();
                        return false;
                    }
                    tx = end;
                }
            }
            // Commit only after all eight scanlines reached the panel.
            if (s->presented_hashes)
                for (unsigned tx = 0; tx < 100; ++tx)
                    if (changed[tx]) s->presented_hashes[ty * s->hash_stride + tx] = hashes[tx];
            if (ty % 4 == 3) yield();
        }
        memcpy(palette, s->palette_rgb565, sizeof(palette));
        pixels = s->data;
        valid = true;
        return true;
    }
};
