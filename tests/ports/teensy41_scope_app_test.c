#include "solar_os_scope.h"
#include "solar_os_scope_model.h"
#include "solar_os_gfx.h"
#include "solar_os_keys.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned frames, closes, cancels, arms, takes;
static bool available, fail_capture;
static size_t width=800, height=480;
static char labels[2048];
static FILE *svg;
static unsigned color;
solar_os_gfx_t *solar_os_context_gfx(solar_os_context_t *c) { return c->gfx; }
void solar_os_context_set_graphics_active(solar_os_context_t *c, bool on) { c->graphics_active=on; }
void solar_os_context_finish(solar_os_context_t *c, int code, const char *s) {
    c->exit_requested=true; c->exit_code=code;
    if (s) snprintf(c->status_message,sizeof(c->status_message),"%s",s);
}
size_t solar_os_gfx_width(const solar_os_gfx_t *g) { (void)g; return width; }
size_t solar_os_gfx_height(const solar_os_gfx_t *g) { (void)g; return height; }
void solar_os_gfx_clear(solar_os_gfx_t *g, solar_os_gfx_color_t c) { (void)g;(void)c; labels[0]=0; }
void solar_os_gfx_set_color(solar_os_gfx_t *g, solar_os_gfx_color_t c) { (void)g; color=c; }
void solar_os_gfx_set_font(solar_os_gfx_t *g, solar_os_gfx_font_t f) { (void)g;(void)f; }
void solar_os_gfx_text(solar_os_gfx_t *g, int x, int y, const char *s) {
    (void)g; assert(x>=0 && y>=0 && x<(int)width && y<(int)height);
    assert(strlen(labels)+strlen(s)+2<sizeof(labels)); strcat(labels,s); strcat(labels,"\n");
    if(svg) fprintf(svg,"<text x='%d' y='%d' font-family='monospace' font-size='12'>%s</text>\n",x,y,s);
}
void solar_os_gfx_line(solar_os_gfx_t *g, int x, int y, int x1, int y1) {
    (void)g; assert(x>=0 && y>=0 && x1>=0 && y1>=0 && x<(int)width && x1<(int)width && y<(int)height && y1<(int)height);
    if(svg) fprintf(svg,"<path d='M%d %d L%d %d' stroke='%s'/>\n",x,y,x1,y1,color==SOLAR_OS_GFX_COLOR_LIGHT?"#ccc":color==SOLAR_OS_GFX_COLOR_DARK?"#888":"black");
}
void solar_os_gfx_present(solar_os_gfx_t *g) { (void)g; ++frames; }
bool solar_scope_capture_open(void) { return available; }
bool solar_scope_capture_arm(uint32_t rate) { (void)rate; ++arms; return true; }
int solar_scope_capture_take(uint16_t *out, size_t count, uint32_t *rate) {
    ++takes; if(fail_capture) return -1;
    solar_scope_demo(out,count,*rate,0,0); return 1;
}
void solar_scope_capture_cancel(void) { ++cancels; }
void solar_scope_capture_close(void) { ++closes; }
int solar_scope_capture_pin(void) { return 40; }
static void begin(solar_os_context_t *c) {
    *solar_os_scope_app.state_slot=calloc(1,solar_os_scope_app.state_size);
    assert(*solar_os_scope_app.state_slot);
    assert(solar_os_scope_app.start(c)==ESP_OK);
}
static void end(solar_os_context_t *c) {
    solar_os_scope_app.stop(c); free(*solar_os_scope_app.state_slot);
    *solar_os_scope_app.state_slot=NULL; assert(!c->graphics_active);
}
static void key(solar_os_context_t *c, char ch) {
    solar_os_event_t e={.type=SOLAR_OS_EVENT_CHAR,.data.ch=ch};
    assert(solar_os_scope_app.event(c,&e));
}
static void tick(solar_os_context_t *c) {
    solar_os_event_t e={.type=SOLAR_OS_EVENT_TICK,.data.tick_ms=100};
    assert(solar_os_scope_app.event(c,&e));
}
int main(int argc, char **argv) {
    solar_os_context_t c={.argc=2,.gfx=(void*)1}; strcpy(c.argv[1],"--demo");
    begin(&c); tick(&c); assert(strstr(labels,"triggered") && strstr(labels,"100.000Hz"));
    key(&c,' '); unsigned before=frames; tick(&c); assert(before==frames);
    key(&c,'s'); tick(&c); assert(strstr(labels,"HOLD SINGLE"));
    key(&c,'r'); assert(strstr(labels,"5.000V"));
    key(&c,'r'); assert(strstr(labels,"50.000V"));
    key(&c,' '); tick(&c); assert(strstr(labels,"Vpp"));
    if(argc==2) {
        svg=fopen(argv[1],"w"); assert(svg);
        fprintf(svg,"<svg xmlns='http://www.w3.org/2000/svg' width='800' height='480'><rect width='800' height='480' fill='white'/>\n");
        tick(&c); fprintf(svg,"</svg>\n"); fclose(svg); svg=NULL;
    }
    key(&c,'w'); key(&c,'w'); key(&c,'n'); tick(&c); assert(strstr(labels,"waiting trigger"));
    for(unsigned i=0;i<15;++i) { key(&c,'+'); tick(&c); }
    for(unsigned i=0;i<15;++i) { key(&c,'-'); tick(&c); }
    for(unsigned i=0;i<40;++i) { key(&c,SOLAR_OS_KEY_UP); tick(&c); }
    for(unsigned i=0;i<40;++i) { key(&c,SOLAR_OS_KEY_DOWN); tick(&c); }
    key(&c,'q'); assert(c.exit_requested); end(&c); assert(!arms && !takes && !closes);
    c=(solar_os_context_t){.argc=1,.gfx=(void*)1}; begin(&c);
    assert(c.exit_requested && strstr(c.status_message,"ADC unavailable")); end(&c);
    available=true; c=(solar_os_context_t){.argc=1,.gfx=(void*)1}; begin(&c);
    tick(&c); assert(arms==1); tick(&c); assert(takes==1 && strstr(labels,"triggered"));
    tick(&c); key(&c,' '); assert(cancels); key(&c,' '); tick(&c);
    fail_capture=true; tick(&c); assert(strstr(labels,"capture error") && strstr(labels,"HOLD"));
    end(&c); assert(closes==1);
    c=(solar_os_context_t){.argc=2,.gfx=(void*)1}; strcpy(c.argv[1],"--demo");
    width=320; height=240; begin(&c); tick(&c); end(&c);
    c=(solar_os_context_t){.argc=1};
    *solar_os_scope_app.state_slot=calloc(1,solar_os_scope_app.state_size);
    assert(solar_os_scope_app.start(&c)==ESP_ERR_INVALID_STATE); end(&c);
    puts("scope app: demo, controls, single, normal, range, mock capture cleanup passed");
}
