// Included by peripherals.cpp so the display driver and monitor live in flash.
#pragma once
#include "small_display.h"
#include "small_navigation.h"
#include "small_monitor_row.h"
#if SK_SECONDARY_ST7735
#include "nes_flexio.h"
extern "C" {
#include "solar_os_memory.h"
#include "solar_os_storage.h"
#if SK_ETHERNET
#include "solar_os_network.h"
#endif
#if SK_MESH_CLIENT
#include "solar_os_mesh_service.h"
#endif
#if SK_CLOCK
#include "solar_os_time.h"
#endif
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
static SmallNavigation small_nav;
static uint8_t nes_held;
DMAMEM static sk_small_music_t small_music;
static uint32_t small_music_at;
static bool small_music_toggle;


static void small_monitor_run(void *) {
    SmallMonitorRow previous[9]{};
    uint32_t last_time=portGET_RUN_TIME_COUNTER_VALUE();
    uint32_t last_idle=ulTaskGetIdleRunTimeCounter();
    uint32_t storage_at=millis()-30000;
    uint32_t generation=0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(1000));
        taskENTER_CRITICAL();
        const bool enabled=small_enabled && !small_owned;
        const uint32_t current_generation=small_generation;
        const unsigned page=small_nav.page, slot=small_nav.slot;
        taskEXIT_CRITICAL();
        if(!enabled) {
            for(auto &row:previous) row.valid=false;
            last_time=portGET_RUN_TIME_COUNTER_VALUE();
            last_idle=ulTaskGetIdleRunTimeCounter();
            storage_at=millis()-30000;
            continue;
        }
        if(generation!=current_generation) for(auto &row:previous) row.valid=false;
        const uint32_t started=micros();
        taskENTER_CRITICAL();
        const uint32_t now=portGET_RUN_TIME_COUNTER_VALUE();
        const uint32_t idle=ulTaskGetIdleRunTimeCounter();
        taskEXIT_CRITICAL();
        const uint32_t elapsed=now-last_time, idle_delta=idle-last_idle;
        const unsigned cpu=elapsed ? uint64_t(elapsed-(idle_delta>elapsed?elapsed:idle_delta))*1000/elapsed : 0;
        last_time=now; last_idle=idle;
        char lines[9][27]{};
        snprintf(lines[1],27,"CPU %u.%u%%",cpu/10,cpu%10);
        solar_os_memory_status_t memory{};
        solar_os_memory_get_status(&memory);
        const solar_os_memory_region_status_t *regions[]={&memory.dtcm,&memory.ocram,&memory.external};
        const char *names[]={"DTCM","OCRAM","PSRAM"};
        for(unsigned i=0;i<3;++i)
            snprintf(lines[i+2],27,"%-5s %lu/%lu KiB",names[i],(unsigned long)((regions[i]->total-regions[i]->free)/1024),(unsigned long)(regions[i]->total/1024));
#if SK_CLOCK
        // Time and date share the top row of the 26-column display.
        solar_os_datetime_t date{};
        strcpy(lines[0],"--:--:-- --  ----/--/--");
        if(solar_os_time_get_datetime(&date)==ESP_OK) {
            const unsigned hour=date.hour%12 ? date.hour%12 : 12;
            snprintf(lines[0],27,"%2u:%02u:%02u %s  %04u-%02u-%02u",
                hour,date.minute,date.second,date.hour<12?"am":"pm",
                date.year,date.month,date.day);
        }
#endif
        static char disks[3][27];
        if(page==0 && uint32_t(millis()-storage_at)>=30000) {
            storage_at=millis();
            const char *paths[]={"/sd","/usb","/flash"};
            const char *labels[]={"SD","USB","Flash"};
            for(unsigned i=0;i<3;++i) {
                solar_os_storage_usage_t usage{};
                if(solar_os_storage_get_usage_for_path(paths[i],&usage)==ESP_OK)
                    snprintf(disks[i],27,"%-5s %llu/%llu MiB",labels[i],(unsigned long long)(usage.used_bytes/1048576),(unsigned long long)(usage.total_bytes/1048576));
                else snprintf(disks[i],27,"%-5s unavailable",labels[i]);
            }
        }
        for(unsigned i=0;i<3;++i) strcpy(lines[5+i],disks[i]);
        strcpy(lines[8],"< > Page  Select Home");
        if(page!=0) {
            for(unsigned i=1;i<8;++i) lines[i][0]=0;
            if(page==1) {
                strcpy(lines[1],"Network");
#if SK_ETHERNET
                solar_os_network_path_info_t path{};
                if(solar_os_network_path_list(&path,1)) {
                    snprintf(lines[2],27,"%.12s: %s",path.name,path.ready?"ready":path.connecting?"connecting":"down");
                    esp_netif_ip_info_t ip{};
                    esp_netif_dns_info_t dns{};
                    char addr[16];
                    if(esp_netif_get_ip_info(path.netif,&ip)==ESP_OK) {
                        esp_ip4addr_ntoa(&ip.ip,addr,sizeof(addr)); snprintf(lines[3],27,"IP  %s",addr);
                        esp_ip4addr_ntoa(&ip.gw,addr,sizeof(addr)); snprintf(lines[4],27,"GW  %s",addr);
                        esp_ip4addr_ntoa(&ip.netmask,addr,sizeof(addr)); snprintf(lines[5],27,"Mask %s",addr);
                    }
                    if(esp_netif_get_dns_info(path.netif,ESP_NETIF_DNS_MAIN,&dns)==ESP_OK) {
                        esp_ip4addr_ntoa(&dns.ip.u_addr.ip4,addr,sizeof(addr)); snprintf(lines[6],27,"DNS %s",addr);
                    }
                } else strcpy(lines[2],"No network interface");
#else
                strcpy(lines[2],"Network unavailable");
#endif
            } else if(page==2) {
                strcpy(lines[1],"Music");
                sk_small_music_t music{};
                taskENTER_CRITICAL();
                if(uint32_t(millis()-small_music_at)<2000) music=small_music;
                taskEXIT_CRITICAL();
                if(music.active) {
                    snprintf(lines[2],27,"%s",music.playing?(music.paused?"Paused":"Playing"):"Stopped");
                    snprintf(lines[3],27,"%.26s",music.track);
                    if(strlen(music.track)>26) snprintf(lines[4],27,"%.26s",music.track+26);
                    const unsigned elapsed=music.elapsed_ms/1000, total=music.total_ms/1000;
                    snprintf(lines[5],27,"%u:%02u / %u:%02u",elapsed/60,elapsed%60,total/60,total%60);
                    snprintf(lines[6],27,"Volume %u%%",music.volume);
                    strcpy(lines[8],"< > Page  A Play/Pause");
                } else {
                    strcpy(lines[2],"Player is not active");
                    strcpy(lines[4],"Open player in a session");
                }
            } else {
                snprintf(lines[1],27,"Mesh slot%u",slot);
#if SK_MESH_CLIENT
                solar_mesh_status_t radio{};
                if(solar_os_mesh_service_status(slot,&radio)) {
                    snprintf(lines[2],27,"%.12s %s",radio.driver,radio.ready?"ready":"waiting");
                    snprintf(lines[3],27,"%.26s",radio.name);
                    snprintf(lines[4],27,"RX %lu TX %lu messages",(unsigned long)radio.messages_rx,(unsigned long)radio.messages_tx);
                    snprintf(lines[5],27,"Nodes %u Channels %u",radio.node_count,radio.channel_count);
                    if(radio.has_rssi) snprintf(lines[6],27,"RSSI %d dBm",radio.rssi);
                    if(radio.has_snr) {
                        const unsigned snr=radio.snr_q4<0 ? -int64_t(radio.snr_q4) : radio.snr_q4;
                        snprintf(lines[7],27,"SNR %s%u.%02u dB",radio.snr_q4<0?"-":"",snr/4,(snr%4)*25);
                    }
                    else if(radio.error[0]) snprintf(lines[7],27,"%.26s",radio.error);
                } else strcpy(lines[2],"Radio disconnected");
#else
                strcpy(lines[2],"Mesh unavailable");
#endif
                strcpy(lines[8],"< > Page  Up/Down Slot");
            }
        }
        unsigned misses=0;
        for(unsigned i=0;i<9;++i) {
            uint16_t color=(i==0 || i==1 || i==8)?ST7735_CYAN:ST7735_WHITE;
            if(page==0 && i==2 && memory.dtcm.free<SOLAR_OS_MEMORY_DTCM_LOW_BYTES) color=ST7735_YELLOW;
            if(page==0 && i==2 && memory.dtcm.free<SOLAR_OS_MEMORY_DTCM_CRITICAL_BYTES) color=ST7735_RED;
            // Alarm color is part of the cache even when rounded KiB are unchanged.
            if(previous[i].matches(lines[i],color)) continue;
            if(!sk_spi_lock(superkeyboard::secondary_spi)) { ++misses; continue; }
            if(small_owned) { sk_spi_unlock(superkeyboard::secondary_spi); break; }
            if(generation!=small_generation) {
                secondary.fillScreen(ST7735_BLACK);
                generation=small_generation;
                for(auto &row:previous) row.valid=false;
            }
            previous[i].draw(secondary,i,lines[i],color,i==8?ST7735_BLUE:ST7735_BLACK);
            sk_spi_unlock(superkeyboard::secondary_spi);
        }
        taskENTER_CRITICAL();
        memcpy(small_lines,lines,sizeof(lines));
        small_cpu=cpu; small_elapsed=micros()-started; small_misses+=misses; ++small_updates;
        taskEXIT_CRITICAL();
    }
}
static void small_monitor_start() {
    nes_reader::begin();
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
    snprintf(out,size,"small=%s updates=%lu cpu=%lu.%lu%% update-us=%lu runtime-us=%lu spi-misses=%lu stack-free=%lu nes=%s nes-scans=%lu nes-faults=%lu",
        small_task?(active?"on":"off"):"unavailable",(unsigned long)updates,(unsigned long)(cpu/10),(unsigned long)(cpu%10),
        (unsigned long)elapsed,(unsigned long)runtime,(unsigned long)misses,
        small_task?(unsigned long)(uxTaskGetStackHighWaterMark(small_task)*sizeof(StackType_t)):0UL,
        nes_reader::mode,(unsigned long)nes_reader::scans,(unsigned long)nes_reader::faults);
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

extern "C" void sk_small_music_publish(const sk_small_music_t *status) {
#if SK_SECONDARY_ST7735
    taskENTER_CRITICAL();
    if(status) small_music=*status;
    else { memset(&small_music,0,sizeof(small_music)); small_music_toggle=false; }
    small_music_at=millis();
    taskEXIT_CRITICAL();
#else
    (void)status;
#endif
}
extern "C" bool sk_small_music_take_toggle() {
#if SK_SECONDARY_ST7735
    taskENTER_CRITICAL(); const bool pending=small_music_toggle && small_music.active && uint32_t(millis()-small_music_at)<2000; small_music_toggle=false; taskEXIT_CRITICAL();
    return pending;
#else
    return false;
#endif
}
extern "C" bool sk_small_page(unsigned page) {
#if SK_SECONDARY_ST7735
    if(page>=4 || !small_task) return false;
    taskENTER_CRITICAL(); small_nav.page=page; ++small_generation; taskEXIT_CRITICAL();
    xTaskNotifyGive(small_task);
    return true;
#else
    (void)page; return false;
#endif
}
extern "C" void sk_small_input_poll() {
#if SK_SECONDARY_ST7735
    if(!small_task) return;
    static bool sampling;
    const uint32_t now=millis();
    taskENTER_CRITICAL();
    if(sampling) { taskEXIT_CRITICAL(); return; }
    sampling=true;
    taskEXIT_CRITICAL();
    uint8_t raw=0;
    uint32_t at=now;
    if(!nes_reader::poll(now,raw,at)) {
        taskENTER_CRITICAL(); sampling=false; taskEXIT_CRITICAL();
        return;
    }
    // A missing controller floats high; a grounded data line is not eight keys.
    const uint8_t buttons=raw==255?0:sk_nes_buttons(raw);
    taskENTER_CRITICAL();
    const unsigned old_page=small_nav.page,old_slot=small_nav.slot;
    const uint8_t pressed=small_nav.update(buttons,at,small_owned || !small_enabled);
    nes_held=small_nav.held;
    sampling=false;
    if(small_nav.page==2 && old_page==2 && (pressed&1) && small_music.active && uint32_t(now-small_music_at)<2000)
        small_music_toggle=true;
    const bool changed=old_page!=small_nav.page || old_slot!=small_nav.slot;
    if(changed) ++small_generation;
    taskEXIT_CRITICAL();
    if(changed || (pressed&1)) xTaskNotifyGive(small_task);
#endif
}

extern "C" void sk_small_input_stop() {
#if SK_SECONDARY_ST7735
    // Final power-cut path: scheduling cannot resume a partially executed poll.
    taskENTER_CRITICAL();
    nes_reader::stop();
    nes_held=0;
    taskEXIT_CRITICAL();
#endif
}
