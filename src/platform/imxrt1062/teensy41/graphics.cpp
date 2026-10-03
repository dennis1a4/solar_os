#if SK_GRAPHICS
#include <arduino_freertos.h>
#include "graphics_presenter.h"
extern "C" {
#include "solar_os_gfx_internal.h"
#include "solar_os_display.h"
#include "solar_os_memory.h"
#include "solar_os_webp_decoder.h"
void sk_lcd_graphics_mode(bool);
bool sk_lcd_pixels(int,int,int,int,const uint16_t *);
void sk_lcd_graphics_presented(uint32_t);
void sk_console_delay_ms(uint32_t);
void __real_solar_os_gfx_prepare_surface(solar_os_gfx_t *);
void __real_solar_os_gfx_release_surface(solar_os_gfx_t *);
}
extern "C" esp_err_t solar_os_display_set_high_refresh_override(const char *,bool,uint16_t){return ESP_ERR_NOT_SUPPORTED;}
static u8g2_t canvas;
static solar_os_gfx_t gfx;
static u8x8_display_info_t info;
static SkGraphicsPresenter presenter;
solar_os_gfx_t *sk_lcd_gfx() {
    if (!gfx.u8g2) {
        auto *mask=(uint8_t *)solar_os_memory_calloc(1,800*480/8,SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"gfx.font-mask");
        configASSERT(mask);
        info.tile_width=100;info.tile_height=60;info.pixel_width=800;info.pixel_height=480;
        canvas.u8x8.display_info=&info;
        u8g2_SetupBuffer(&canvas,mask,60,u8g2_ll_hvline_vertical_top_lsb,U8G2_R0);
        solar_os_gfx_init(&gfx,&canvas);
    }
    return &gfx;
}
extern "C" void sk_gfx_invalidate_presenter() { presenter.reset(); }
extern "C" void __wrap_solar_os_gfx_prepare_surface(solar_os_gfx_t *g) {
    __real_solar_os_gfx_prepare_surface(g);sk_lcd_graphics_mode(true);
}
extern "C" void __wrap_solar_os_gfx_release_surface(solar_os_gfx_t *g) {
    presenter.reset();__real_solar_os_gfx_release_surface(g);sk_lcd_graphics_mode(false);
}
extern "C" esp_err_t solar_os_display_get_colors(uint32_t *fg,uint32_t *bg) { if(fg)*fg=0;if(bg)*bg=0xffffff;return ESP_OK; }
extern "C" bool solar_os_display_target_name_for_u8g2(const u8g2_t *u,char *name,size_t n) {
    if(u!=&canvas || n<9)return false;strlcpy(name,"display0",n);return true;
}
extern "C" bool solar_os_display_find_target(const char *name,solar_os_display_target_t *target) {
    if(!name || strcmp(name,"display0") || !target)return false;
    memset(target,0,sizeof(*target));strcpy(target->name,"display0");target->ready=true;
    target->width=800;target->height=480;target->u8g2=&canvas;
    target->surface_formats=SOLAR_OS_DISPLAY_FORMAT_INDEX8_BIT;return true;
}
extern "C" esp_err_t solar_os_display_present_surface(u8g2_t *u,const solar_os_display_surface_t *s) {
    if(u!=&canvas)return ESP_ERR_INVALID_ARG;
    uint32_t start=millis();
    bool ok=presenter.present(s,
        [](unsigned x,unsigned y,unsigned w,const uint16_t *p) {
            return sk_lcd_pixels(x,y,w,1,p);
        }, [] { sk_console_delay_ms(1); });
    if(!ok)return ESP_FAIL;
    sk_lcd_graphics_presented(millis()-start);return ESP_OK;
}

extern "C" void solar_os_display_present(u8g2_t *,solar_os_display_present_mode_t) {}
extern "C" esp_err_t solar_os_display_present_frame(u8g2_t *u,const solar_os_display_raster_t *f) {
    if(u!=&canvas || !f || !f->data || !f->source_width || !f->source_height || !f->width || !f->height)return ESP_ERR_INVALID_ARG;
    unsigned bits=f->format==SOLAR_OS_DISPLAY_FORMAT_INDEX8?8:f->format==SOLAR_OS_DISPLAY_FORMAT_INDEX2?2:f->format==SOLAR_OS_DISPLAY_FORMAT_MONO1?1:0;
    unsigned colors=1U<<bits;
    if(!bits || f->source_stride<(f->source_width*bits+7)/8 || f->data_size<size_t(f->source_stride)*f->source_height || (bits>1 && (!f->palette_rgb565 || f->palette_size<colors)))return ESP_ERR_INVALID_SIZE;
    auto color=[&](unsigned index) {
        if(f->palette_rgb565 && index<f->palette_size) {
            uint16_t c=f->palette_rgb565[index];return solar_os_gfx_rgb(((c>>11)*255)/31,(((c>>5)&63)*255)/63,((c&31)*255)/31);
        }
        return solar_os_gfx_color_t((bool(index)^f->palette_inverted)?SOLAR_OS_GFX_COLOR_BLACK:SOLAR_OS_GFX_COLOR_WHITE);
    };
    solar_os_gfx_prepare_surface(&gfx);
    if(f->clear_background)solar_os_gfx_clear(&gfx,color(f->background_index));
    for(unsigned y=0;y<f->height && y+f->y<480;++y) {
        unsigned sy=uint32_t(y)*f->source_height/f->height;
        for(unsigned x=0;x<f->width && x+f->x<800;++x) {
            unsigned sx=uint32_t(x)*f->source_width/f->width;
            unsigned index=(f->data[sy*f->source_stride+sx*bits/8]>>((sx*bits)%8))&(colors-1);
            solar_os_gfx_set_color(&gfx,color(index));solar_os_gfx_pixel(&gfx,x+f->x,y+f->y);
        }
    }
    solar_os_gfx_present(&gfx);return ESP_OK;
}
extern "C" esp_err_t solar_os_display_present_mono_xbm(u8g2_t *u,const uint8_t *p,size_t size,uint16_t x,uint16_t y,uint16_t w,uint16_t h,uint16_t stride,bool inverted) {
    solar_os_display_raster_t f{};f.data=p;f.data_size=size;f.source_width=w;f.source_height=h;f.source_stride=stride;
    f.x=x;f.y=y;f.width=w;f.height=h;f.format=SOLAR_OS_DISPLAY_FORMAT_MONO1;f.palette_inverted=inverted;
    return solar_os_display_present_frame(u,&f);
}
extern "C" void solar_os_splash_draw_reboot(solar_os_gfx_t *g,const char *status) {
    solar_os_gfx_clear(g,SOLAR_OS_GFX_COLOR_BLACK);solar_os_gfx_set_color(g,SOLAR_OS_GFX_COLOR_WHITE);
    solar_os_gfx_text(g,0,16,status?status:"Rebooting");solar_os_gfx_present(g);
}
// WebP is not enabled in this profile; PNG/JPEG/GIF use the shared stb decoder.
extern "C" esp_err_t solar_os_webp_decode_gray(const uint8_t *,size_t,uint32_t,uint8_t **,uint32_t *,uint32_t *) {return ESP_ERR_NOT_SUPPORTED;}
extern "C" esp_err_t solar_os_webp_decode_rgb(const uint8_t *,size_t,uint32_t,uint8_t **,uint32_t *,uint32_t *) {return ESP_ERR_NOT_SUPPORTED;}
extern "C" void solar_os_webp_free(void *p) {solar_os_memory_free(p);}
#endif
