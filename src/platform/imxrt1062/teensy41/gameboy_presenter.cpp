#if SK_GAMEBOY
#include <arduino_freertos.h>
#include "small_display.h"
extern "C" {
#include "solar_os_gameboy_presenter.h"
#include "solar_os_gameboy_video.h"
#include "solar_os_memory.h"
}
// Synchronous, bounded SPI presentation in the emulator worker. Its 30 Hz
// source cadence leaves alternate 60 Hz emulator frames without display work.
static uint16_t *pixels;
static bool claimed;
static solar_os_gameboy_presenter_stats_t statistics;
static char last_log[224];
struct InputTrace {uint32_t ms; char kind; uint8_t value;};
static InputTrace input_trace[32];
static unsigned trace_count;
static uint32_t poll_time[2],poll_gap[2];
static uint8_t poll_mask[2];
extern "C" void sk_gameboy_trace(char kind,uint8_t value) {
    const uint32_t now=millis();
    taskENTER_CRITICAL();
    bool record=true;
    if(kind=='I' || kind=='E') {
        const unsigned i=kind=='I'?0:1;
        const uint32_t gap=now-poll_time[i];
        if(poll_time[i] && gap>poll_gap[i])poll_gap[i]=gap;
        poll_time[i]=now;record=value!=poll_mask[i];poll_mask[i]=value;
    }
    if(record)input_trace[trace_count++%32]={now,kind,value};
    taskEXIT_CRITICAL();
}
extern "C" void sk_gameboy_input_status(char *out,unsigned size) {
    InputTrace copy[32];unsigned count;uint32_t gaps[2];
    taskENTER_CRITICAL();
    count=trace_count<32?trace_count:32;
    for(unsigned i=0;i<count;++i)copy[i]=input_trace[(trace_count-count+i)%32];
    gaps[0]=poll_gap[0];gaps[1]=poll_gap[1];
    taskEXIT_CRITICAL();
    snprintf(out,size,"max-input-gap-ms=%lu max-frame-gap-ms=%lu; I=input E=emulator D=display\n",
        (unsigned long)gaps[0],(unsigned long)gaps[1]);
    for(unsigned i=0;i<count;++i) {
        const size_t used=strlen(out);
        if(used<size)snprintf(out+used,size-used,"%lu %c %02x\n",(unsigned long)copy[i].ms,copy[i].kind,copy[i].value);
    }
}
extern "C" void sk_gameboy_log(const char *text) {
    taskENTER_CRITICAL();strlcpy(last_log,text,sizeof(last_log));taskEXIT_CRITICAL();
}
extern "C" void sk_gameboy_status(char *out,unsigned size) {
    taskENTER_CRITICAL();strlcpy(out,last_log,size);taskEXIT_CRITICAL();
}
extern "C" esp_err_t solar_os_gameboy_presenter_init(solar_os_gfx_t *) {
    if(pixels) return ESP_ERR_INVALID_STATE;
    pixels=static_cast<uint16_t *>(solar_os_memory_calloc(160*128,2,SOLAR_OS_MEMORY_INTERNAL_PREFERRED,"gameboy.rgb"));
    if(!pixels) return ESP_ERR_NO_MEM;
    if(!sk_small_acquire()) {solar_os_memory_free(pixels);pixels=nullptr;return ESP_ERR_INVALID_STATE;}
    claimed=true;statistics={};trace_count=0;
    memset(poll_time,0,sizeof(poll_time));memset(poll_gap,0,sizeof(poll_gap));memset(poll_mask,0,sizeof(poll_mask));
    return ESP_OK;
}
extern "C" esp_err_t solar_os_gameboy_presenter_resume() {
    if(!pixels)return ESP_ERR_INVALID_STATE;
    if(!claimed)claimed=sk_small_acquire();
    return claimed?ESP_OK:ESP_ERR_INVALID_STATE;
}
extern "C" void solar_os_gameboy_presenter_suspend() {
    if(claimed) {sk_small_release();claimed=false;}
}
extern "C" void solar_os_gameboy_presenter_deinit() {
    solar_os_gameboy_presenter_suspend();solar_os_memory_free(pixels);pixels=nullptr;
}
extern "C" bool solar_os_gameboy_presenter_queue(const uint8_t *bitmap) {
    if(!pixels || !claimed || !bitmap)return false;
    const uint32_t start=micros();
    const uint16_t palette[]={0xffff,0xad55,0x52aa,0x0000};
    bool changed=false;
    // 160x144 -> 142x128, centered with nine-pixel black side borders.
    for(unsigned y=0;y<128;++y) {
        const unsigned sy=y*144/128;
        for(unsigned x=0;x<142;++x) {
            const unsigned sx=x*160/142;
            const uint16_t color=palette[(bitmap[sy*40+sx/4]>>((sx%4)*2))&3];
            changed|=pixels[y*160+9+x]!=color;
            pixels[y*160+9+x]=color;
        }
    }
    const bool ok=sk_small_frame(pixels);
    if(ok && changed)sk_gameboy_trace('D',0);
    statistics.present_us+=uint32_t(micros()-start);
    if(ok)++statistics.presented_frames;else ++statistics.dropped_frames;
    statistics.last_error=ok?ESP_OK:ESP_ERR_TIMEOUT;
    return ok;
}
extern "C" void solar_os_gameboy_presenter_take_stats(solar_os_gameboy_presenter_stats_t *out) {
    if(out){*out=statistics;statistics={};}
}
#endif
