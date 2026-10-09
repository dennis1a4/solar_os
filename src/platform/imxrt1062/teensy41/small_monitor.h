// Included by peripherals.cpp so the display driver and monitor live in flash.
#pragma once
#include "small_display.h"
#if SK_SECONDARY_ST7735
extern "C" {
#include "solar_os_memory.h"
#include "solar_os_storage.h"
}

// Keep the permanent stack out of scarce DTCM; no framebuffer or heap allocation.
DMAMEM static StackType_t small_stack[1024];
static StaticTask_t small_tcb;
static TaskHandle_t small_task;
static bool small_enabled=true;
static bool small_owned; // Protected by SPI mutex for rendering.
static uint32_t small_generation;
static uint32_t small_updates, small_cpu, small_elapsed, small_misses;
static char small_lines[9][27];

static void small_monitor_run(void *) {
    char previous[9][27]{};
    uint32_t last_time=portGET_RUN_TIME_COUNTER_VALUE();
    uint32_t last_idle=ulTaskGetIdleRunTimeCounter();
    TickType_t wake=xTaskGetTickCount();
    unsigned storage_age=30;
    uint32_t generation=0;
    for (;;) {
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(1000));
        taskENTER_CRITICAL();
        const bool enabled=small_enabled && !small_owned;
        const uint32_t current_generation=small_generation;
        taskEXIT_CRITICAL();
        if(!enabled) {
            memset(previous,0,sizeof(previous));
            last_time=portGET_RUN_TIME_COUNTER_VALUE();
            last_idle=ulTaskGetIdleRunTimeCounter();
            storage_age=30;
            continue;
        }
        if(generation!=current_generation) memset(previous,0,sizeof(previous));
        const uint32_t started=micros();
        taskENTER_CRITICAL();
        const uint32_t now=portGET_RUN_TIME_COUNTER_VALUE();
        const uint32_t idle=ulTaskGetIdleRunTimeCounter();
        taskEXIT_CRITICAL();
        const uint32_t elapsed=now-last_time, idle_delta=idle-last_idle;
        const unsigned cpu=elapsed ? uint64_t(elapsed-(idle_delta>elapsed?elapsed:idle_delta))*1000/elapsed : 0;
        last_time=now; last_idle=idle;
        char lines[9][27]{};
        snprintf(lines[0],27,"SolarOS  CPU %u.%u%%",cpu/10,cpu%10);
        strcpy(lines[1],"RAM used/total KiB");
        solar_os_memory_status_t memory{};
        solar_os_memory_get_status(&memory);
        const solar_os_memory_region_status_t *regions[]={&memory.dtcm,&memory.ocram,&memory.external};
        const char *names[]={"DTCM","OCRAM","PSRAM"};
        for(unsigned i=0;i<3;++i)
            snprintf(lines[i+2],27,"%-5s %lu/%lu",names[i],(unsigned long)((regions[i]->total-regions[i]->free)/1024),(unsigned long)(regions[i]->total/1024));
        strcpy(lines[5],"Disk used/total MiB");
        static char disks[3][27];
        if(++storage_age>=30) {
            storage_age=0;
            const char *paths[]={"/sd","/usb","/flash"};
            const char *labels[]={"SD","USB","Flash"};
            for(unsigned i=0;i<3;++i) {
                solar_os_storage_usage_t usage{};
                if(solar_os_storage_get_usage_for_path(paths[i],&usage)==ESP_OK)
                    snprintf(disks[i],27,"%-5s %llu/%llu",labels[i],(unsigned long long)(usage.used_bytes/1048576),(unsigned long long)(usage.total_bytes/1048576));
                else snprintf(disks[i],27,"%-5s unavailable",labels[i]);
            }
        }
        for(unsigned i=0;i<3;++i) strcpy(lines[6+i],disks[i]);
        unsigned misses=0;
        for(unsigned i=0;i<9;++i) {
            // Include DTCM alarm changes even when rounded KiB are unchanged.
            if(i!=2 && !strcmp(previous[i],lines[i])) continue;
            if(!sk_spi_lock(superkeyboard::secondary_spi)) { ++misses; continue; }
            if(small_owned) { sk_spi_unlock(superkeyboard::secondary_spi); break; }
            if(generation!=small_generation) {
                secondary.fillScreen(ST7735_BLACK);
                generation=small_generation;
                memset(previous,0,sizeof(previous));
            }
            const unsigned y=i*14;
            secondary.fillRect(0,y,secondary.width(),12,ST7735_BLACK);
            uint16_t color=(i==1 || i==5)?ST7735_CYAN:ST7735_WHITE;
            if(i==2 && memory.dtcm.free<SOLAR_OS_MEMORY_DTCM_LOW_BYTES) color=ST7735_YELLOW;
            if(i==2 && memory.dtcm.free<SOLAR_OS_MEMORY_DTCM_CRITICAL_BYTES) color=ST7735_RED;
            secondary.setTextColor(color);
            secondary.setCursor(0,y+2);
            secondary.print(lines[i]);
            sk_spi_unlock(superkeyboard::secondary_spi);
            strcpy(previous[i],lines[i]);
        }
        taskENTER_CRITICAL();
        memcpy(small_lines,lines,sizeof(lines));
        small_cpu=cpu; small_elapsed=micros()-started; small_misses+=misses; ++small_updates;
        taskEXIT_CRITICAL();
    }
}
static void small_monitor_start() {
    small_task=xTaskCreateStatic(small_monitor_run,"small-monitor",1024,nullptr,1,small_stack,&small_tcb);
    configASSERT(small_task);
}
#endif

void sk_small_monitor_status(char *out,size_t size,int enabled) {
#if SK_SECONDARY_ST7735
    taskENTER_CRITICAL();
    if(enabled>=0) small_enabled=enabled!=0;
    char lines[9][27];
    memcpy(lines,small_lines,sizeof(lines));
    const bool active=small_enabled;
    const uint32_t updates=small_updates,cpu=small_cpu,elapsed=small_elapsed,misses=small_misses;
    taskEXIT_CRITICAL();
    const uint32_t runtime=small_task?ulTaskGetRunTimeCounter(small_task):0;
    snprintf(out,size,"small=%s updates=%lu cpu=%lu.%lu%% update-us=%lu runtime-us=%lu spi-misses=%lu stack-free=%lu",
        small_task?(active?"on":"off"):"unavailable",(unsigned long)updates,(unsigned long)(cpu/10),(unsigned long)(cpu%10),
        (unsigned long)elapsed,(unsigned long)runtime,(unsigned long)misses,
        small_task?(unsigned long)(uxTaskGetStackHighWaterMark(small_task)*sizeof(StackType_t)):0UL);
    for(unsigned i=0;i<9;++i) {
        const size_t used=strlen(out);
        if(used<size) snprintf(out+used,size-used,"\n%s",lines[i]);
    }
#else
    (void)enabled;
    snprintf(out,size,"Small display unavailable in this build");
#endif
}

extern "C" bool sk_small_acquire() {
#if SK_SECONDARY_ST7735
    if(!small_task || !sk_spi_lock(superkeyboard::secondary_spi)) return false;
    const bool available=!small_owned;
    if(available) {
        taskENTER_CRITICAL(); small_owned=true; taskEXIT_CRITICAL();
        secondary.fillScreen(ST7735_BLACK);
    }
    sk_spi_unlock(superkeyboard::secondary_spi);
    return available;
#else
    return false;
#endif
}
extern "C" void sk_small_release() {
#if SK_SECONDARY_ST7735
    // No in-flight graphics producer may remain when its owner releases.
    while(!sk_spi_lock(superkeyboard::secondary_spi)) vTaskDelay(1);
    taskENTER_CRITICAL(); small_owned=false; ++small_generation; taskEXIT_CRITICAL();
    sk_spi_unlock(superkeyboard::secondary_spi);
#endif
}
extern "C" bool sk_small_frame(const uint16_t *pixels) {
#if SK_SECONDARY_ST7735
    if(!pixels || !sk_spi_lock(superkeyboard::secondary_spi)) return false;
    const bool ok=small_owned;
    if(ok) secondary.writeRect(0,0,160,128,pixels);
    sk_spi_unlock(superkeyboard::secondary_spi);
    return ok;
#else
    (void)pixels; return false;
#endif
}
