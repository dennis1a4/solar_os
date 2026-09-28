#if SK_GRAPHICS
#include <string.h>
#include "py/runtime.h"
#include "py/objstr.h"
#include "solar_os.h"
#include "solar_os_gfx_internal.h"
#include "solar_os_keys.h"
#include "solar_os_vt100.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern bool sk_python_poll_cancel(void);
extern void sk_console_delay_ms(uint32_t);
static solar_os_context_t *context;
static bool active;
static solar_os_vt100_input_t input;
static uint8_t keys[32];
static unsigned head,tail;
static TickType_t last_byte;
static bool push(char ch,void *unused) { (void)unused;unsigned next=(head+1)%sizeof(keys);if(next!=tail){keys[head]=(uint8_t)ch;head=next;}return true; }
void sk_python_gfx_key(int ch) { if(active){solar_os_vt100_input_feed_byte(&input,ch,push,NULL);last_byte=xTaskGetTickCount();} }
static solar_os_gfx_t *target(void) {
    solar_os_gfx_t *g=solar_os_context_gfx(context);
    if(!active || !g)mp_raise_msg(&mp_type_RuntimeError,MP_ERROR_TEXT("call gfx.begin() from the LCD shell first"));
    return g;
}
static int coordinate(mp_obj_t v) {int n=mp_obj_get_int(v);if(n < -32767 || n>32767)mp_raise_ValueError(MP_ERROR_TEXT("coordinate out of range"));return n;}
static solar_os_gfx_color_t color_value(mp_obj_t v) {unsigned n=mp_obj_get_int(v);if(!solar_os_gfx_color_is_valid(n))mp_raise_ValueError(MP_ERROR_TEXT("invalid color"));return n;}
static mp_obj_t begin(size_t n,const mp_obj_t *a) {
    if(!solar_os_context_gfx(context))mp_raise_msg(&mp_type_RuntimeError,MP_ERROR_TEXT("graphics requires the LCD shell"));
    if(n && a[0]!=mp_const_none && strcmp(mp_obj_str_get_str(a[0]),"display0"))mp_raise_ValueError(MP_ERROR_TEXT("unknown display"));
    solar_os_context_set_graphics_active(context,true);
    if(!solar_os_gfx_surface(solar_os_context_gfx(context))) {solar_os_context_set_graphics_active(context,false);mp_raise_msg(&mp_type_MemoryError,MP_ERROR_TEXT("graphics canvas allocation failed"));}
    active=true;return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(begin_obj,0,1,begin);
static mp_obj_t end(void) {if(active)solar_os_context_set_graphics_active(context,false);active=false;return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_0(end_obj,end);
static mp_obj_t width(void) {return mp_obj_new_int(solar_os_gfx_width(target()));}
static MP_DEFINE_CONST_FUN_OBJ_0(width_obj,width);
static mp_obj_t height(void) {return mp_obj_new_int(solar_os_gfx_height(target()));}
static MP_DEFINE_CONST_FUN_OBJ_0(height_obj,height);
static mp_obj_t size(void) {mp_obj_t a[]={width(),height()};return mp_obj_new_tuple(2,a);}
static MP_DEFINE_CONST_FUN_OBJ_0(size_obj,size);
static mp_obj_t clear(size_t n,const mp_obj_t *a) {solar_os_gfx_clear(target(),n?color_value(a[0]):SOLAR_OS_GFX_COLOR_WHITE);return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(clear_obj,0,1,clear);
static mp_obj_t color(size_t n,const mp_obj_t *a) {solar_os_gfx_t *g=target();if(n)solar_os_gfx_set_color(g,color_value(a[0]));return mp_obj_new_int(solar_os_gfx_color(g));}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(color_obj,0,1,color);
static mp_obj_t font(size_t n,const mp_obj_t *a) {solar_os_gfx_t *g=target();if(n){int f=mp_obj_get_int(a[0]);if(f<0 || f>=SOLAR_OS_GFX_FONT_COUNT)mp_raise_ValueError(MP_ERROR_TEXT("invalid font"));solar_os_gfx_set_font(g,f);}return mp_obj_new_int(solar_os_gfx_font(g));}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(font_obj,0,1,font);
static mp_obj_t gray(mp_obj_t a) {int n=mp_obj_get_int(a);return mp_obj_new_int(solar_os_gfx_gray(n<0?0:n>16?16:n));}
static MP_DEFINE_CONST_FUN_OBJ_1(gray_obj,gray);
static mp_obj_t rgb(mp_obj_t r,mp_obj_t g,mp_obj_t b) {int x=mp_obj_get_int(r),y=mp_obj_get_int(g),z=mp_obj_get_int(b);if(x<0 || x>255 || y<0 || y>255 || z<0 || z>255)mp_raise_ValueError(MP_ERROR_TEXT("RGB components must be 0..255"));return mp_obj_new_int(solar_os_gfx_rgb(x,y,z));}
static MP_DEFINE_CONST_FUN_OBJ_3(rgb_obj,rgb);
#define DRAW4(name) static mp_obj_t name(size_t n,const mp_obj_t *a){(void)n;solar_os_gfx_##name(target(),coordinate(a[0]),coordinate(a[1]),coordinate(a[2]),coordinate(a[3]));return mp_const_none;} static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(name##_obj,4,4,name)
DRAW4(line);DRAW4(rect);DRAW4(fill_rect);
#define DRAW3(name) static mp_obj_t name(mp_obj_t x,mp_obj_t y,mp_obj_t r){int radius=coordinate(r);if(radius<0 || radius>1600)mp_raise_ValueError(MP_ERROR_TEXT("radius out of range"));solar_os_gfx_##name(target(),coordinate(x),coordinate(y),radius);return mp_const_none;} static MP_DEFINE_CONST_FUN_OBJ_3(name##_obj,name)
DRAW3(circle);DRAW3(fill_circle);
static mp_obj_t pixel(mp_obj_t x,mp_obj_t y) {solar_os_gfx_pixel(target(),coordinate(x),coordinate(y));return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_2(pixel_obj,pixel);
static mp_obj_t text(mp_obj_t x,mp_obj_t y,mp_obj_t s) {solar_os_gfx_text(target(),coordinate(x),coordinate(y),mp_obj_str_get_str(s));return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_3(text_obj,text);
static mp_obj_t present(void) {solar_os_gfx_present(target());if(sk_python_poll_cancel())mp_raise_type(&mp_type_KeyboardInterrupt);return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_0(present_obj,present);
static mp_obj_t bitmap(size_t n,const mp_obj_t *a) {(void)n;int w=coordinate(a[2]),h=coordinate(a[3]);mp_buffer_info_t data;mp_get_buffer_raise(a[4],&data,MP_BUFFER_READ);if(w<=0 || h<=0 || data.len>128 || (size_t)((w+7)/8)*h!=data.len)mp_raise_ValueError(MP_ERROR_TEXT("invalid bitmap dimensions or length"));solar_os_gfx_bitmap(target(),coordinate(a[0]),coordinate(a[1]),w,h,data.buf);return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(bitmap_obj,5,5,bitmap);
static mp_obj_t icon(size_t n,const mp_obj_t *a) {(void)n;solar_os_gfx_icon_t i;int s=mp_obj_get_int(a[3]);if(solar_os_gfx_icon_from_name(mp_obj_str_get_str(a[2]),&i)!=ESP_OK || (s!=8 && s!=16 && s!=32 && s!=48 && s!=64))mp_raise_ValueError(MP_ERROR_TEXT("invalid icon or size"));solar_os_gfx_icon(target(),coordinate(a[0]),coordinate(a[1]),i,s);return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(icon_obj,4,4,icon);
static mp_obj_t getch(size_t n,const mp_obj_t *a) {
    target();int timeout=n?mp_obj_get_int(a[0]):0;if(timeout<0 || timeout>60000)mp_raise_ValueError(MP_ERROR_TEXT("timeout must be 0..60000"));
    TickType_t start=xTaskGetTickCount();do {
        if(sk_python_poll_cancel())mp_raise_type(&mp_type_KeyboardInterrupt);
        if(solar_os_vt100_input_pending(&input) && xTaskGetTickCount()-last_byte>=pdMS_TO_TICKS(40))solar_os_vt100_input_flush(&input,push,NULL);
        if(head!=tail){uint8_t ch=keys[tail];tail=(tail+1)%sizeof(keys);return mp_obj_new_int(ch);}
    }while(xTaskGetTickCount()-start<pdMS_TO_TICKS(timeout));return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(getch_obj,0,1,getch);
static mp_obj_t should_exit(void) {return mp_obj_new_bool(sk_python_poll_cancel());}
static MP_DEFINE_CONST_FUN_OBJ_0(should_exit_obj,should_exit);
static mp_obj_t sleep_ms(mp_obj_t value) {int n=mp_obj_get_int(value);if(n<0 || n>60000)mp_raise_ValueError(MP_ERROR_TEXT("sleep out of range"));for(int i=0;i<n;i+=10){if(sk_python_poll_cancel())mp_raise_type(&mp_type_KeyboardInterrupt);sk_console_delay_ms(n-i<10?n-i:10);}return mp_const_none;}
static MP_DEFINE_CONST_FUN_OBJ_1(sleep_ms_obj,sleep_ms);
static mp_obj_t ticks_ms(void) {return mp_obj_new_int_from_uint(xTaskGetTickCount()*portTICK_PERIOD_MS);}
static MP_DEFINE_CONST_FUN_OBJ_0(ticks_ms_obj,ticks_ms);
static void put(mp_obj_t module,const char *name,mp_obj_t value) {mp_obj_dict_store(MP_OBJ_FROM_PTR(((mp_obj_module_t *)MP_OBJ_TO_PTR(module))->globals),MP_OBJ_NEW_QSTR(qstr_from_str(name)),value);}
void sk_python_gfx_init(solar_os_context_t *ctx) {
    context=ctx;active=false;head=tail=0;solar_os_vt100_input_reset(&input);
    mp_obj_t root=mp_obj_new_module(qstr_from_str("solaros")),module=mp_obj_new_module(qstr_from_str("solaros.gfx"));put(root,"gfx",module);
#define METHOD(name) put(module,#name,MP_OBJ_FROM_PTR(&name##_obj))
    METHOD(begin);METHOD(end);METHOD(width);METHOD(height);METHOD(size);METHOD(clear);METHOD(color);METHOD(font);METHOD(gray);METHOD(rgb);METHOD(pixel);METHOD(line);METHOD(rect);METHOD(fill_rect);METHOD(circle);METHOD(fill_circle);METHOD(text);METHOD(present);METHOD(bitmap);METHOD(icon);METHOD(getch);
#undef METHOD
    put(module,"refresh",MP_OBJ_FROM_PTR(&present_obj));put(module,"sprite",MP_OBJ_FROM_PTR(&bitmap_obj));put(module,"set_color",MP_OBJ_FROM_PTR(&color_obj));put(module,"set_font",MP_OBJ_FROM_PTR(&font_obj));
    put(root,"should_exit",MP_OBJ_FROM_PTR(&should_exit_obj));put(root,"sleep_ms",MP_OBJ_FROM_PTR(&sleep_ms_obj));
    mp_obj_t tm=mp_obj_new_module(qstr_from_str("solaros.time"));put(root,"time",tm);put(tm,"sleep_ms",MP_OBJ_FROM_PTR(&sleep_ms_obj));put(tm,"ticks_ms",MP_OBJ_FROM_PTR(&ticks_ms_obj));
#define CONST(name,value) put(module,name,mp_obj_new_int(value))
    CONST("WHITE",SOLAR_OS_GFX_COLOR_WHITE);CONST("BLACK",SOLAR_OS_GFX_COLOR_BLACK);CONST("LIGHT",SOLAR_OS_GFX_COLOR_LIGHT);CONST("DARK",SOLAR_OS_GFX_COLOR_DARK);CONST("GRAY_MAX",16);
    CONST("FONT_SMALL",SOLAR_OS_GFX_FONT_SMALL);
    CONST("FONT_MONO",SOLAR_OS_GFX_FONT_MONO);
    CONST("FONT_BOLD",SOLAR_OS_GFX_FONT_BOLD);
    CONST("FONT_MONO_12",SOLAR_OS_GFX_FONT_MONO_12);
    CONST("FONT_MONO_14",SOLAR_OS_GFX_FONT_MONO_14);
    CONST("FONT_MONO_16",SOLAR_OS_GFX_FONT_MONO_16);
    CONST("FONT_MONO_18",SOLAR_OS_GFX_FONT_MONO_18);
    CONST("FONT_MONO_20",SOLAR_OS_GFX_FONT_MONO_20);
    CONST("FONT_BOLD_12",SOLAR_OS_GFX_FONT_BOLD_12);
    CONST("FONT_BOLD_14",SOLAR_OS_GFX_FONT_BOLD_14);
    CONST("FONT_BOLD_16",SOLAR_OS_GFX_FONT_BOLD_16);
    CONST("FONT_BOLD_18",SOLAR_OS_GFX_FONT_BOLD_18);
    CONST("FONT_BOLD_20",SOLAR_OS_GFX_FONT_BOLD_20);
    CONST("FONT_ITALIC_12",SOLAR_OS_GFX_FONT_ITALIC_12);
    CONST("FONT_ITALIC_14",SOLAR_OS_GFX_FONT_ITALIC_14);
    CONST("FONT_ITALIC_16",SOLAR_OS_GFX_FONT_ITALIC_16);
    CONST("FONT_ITALIC_18",SOLAR_OS_GFX_FONT_ITALIC_18);
    CONST("FONT_ITALIC_20",SOLAR_OS_GFX_FONT_ITALIC_20);
    CONST("FONT_BOLD_ITALIC_12",SOLAR_OS_GFX_FONT_BOLD_ITALIC_12);
    CONST("FONT_BOLD_ITALIC_14",SOLAR_OS_GFX_FONT_BOLD_ITALIC_14);
    CONST("FONT_BOLD_ITALIC_16",SOLAR_OS_GFX_FONT_BOLD_ITALIC_16);
    CONST("FONT_BOLD_ITALIC_18",SOLAR_OS_GFX_FONT_BOLD_ITALIC_18);
    CONST("FONT_BOLD_ITALIC_20",SOLAR_OS_GFX_FONT_BOLD_ITALIC_20);
    CONST("KEY_ENTER",SOLAR_OS_KEY_ENTER);
    CONST("KEY_ESCAPE",SOLAR_OS_KEY_ESCAPE);
    CONST("KEY_UP",SOLAR_OS_KEY_UP);
    CONST("KEY_DOWN",SOLAR_OS_KEY_DOWN);
    CONST("KEY_LEFT",SOLAR_OS_KEY_LEFT);
    CONST("KEY_RIGHT",SOLAR_OS_KEY_RIGHT);
    CONST("KEY_HOME",SOLAR_OS_KEY_HOME);
    CONST("KEY_END",SOLAR_OS_KEY_END);
    CONST("KEY_PAGE_UP",SOLAR_OS_KEY_PAGE_UP);
    CONST("KEY_PAGE_DOWN",SOLAR_OS_KEY_PAGE_DOWN);
    CONST("KEY_APP_EXIT",SOLAR_OS_KEY_APP_EXIT);
#undef CONST
}
void sk_python_gfx_destroy(void) {end();context=NULL;head=tail=0;}
#endif
