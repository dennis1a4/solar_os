#include "power_path.h"
#if SK_POWER_PATH
#include <arduino_freertos.h>
#include <semphr.h>
#include "platform.h"
#include "board.h"
#include "power_path_model.h"
#include "power_shutdown.h"
extern "C" {
#include "solar_os.h"
#include "solar_os_resources.h"
#include "solar_os_shell_io.h"
#if SK_SETTINGS
#include "nvs.h"
#endif
void sk_power_button_event();
}
namespace {
constexpr unsigned bus=1, interrupt_pin=superkeyboard::power_expander_int;
bool read(void *,uint8_t address,uint8_t reg,uint8_t *out,size_t n) {
    return sk_i2c_transfer(bus,address,&reg,1,out,n)==ESP_OK;
}
bool write(void *,uint8_t address,uint8_t reg,const uint8_t *data,size_t n) {
    if(n>3)return false;
    uint8_t bytes[4];bytes[0]=reg;memcpy(bytes+1,data,n);
    return sk_i2c_transfer(bus,address,bytes,n+1,nullptr,0)==ESP_OK;
}
SkPowerPathDriver driver({nullptr,read,write});
StaticSemaphore_t guard_storage;
SemaphoreHandle_t guard;
DMAMEM StackType_t stack[1024];
StaticTask_t tcb;
SkPowerPathSnapshot published;
SkPowerPdSnapshot published_pd;
uint32_t published_at,pd_published_at;
volatile bool interrupt_pending;
bool claimed,cut_attempted;
void interrupt() {interrupt_pending=true;}
void publish() {
    taskENTER_CRITICAL();published=driver.state;published_at=millis();taskEXIT_CRITICAL();
}
void run(void *) {
    uint32_t gpio_at=millis()-1000,meter_at=gpio_at,pd_at=gpio_at;
    bool button_held=false;
    for(;;) {
        const uint32_t now=millis();
        if(xSemaphoreTake(guard,pdMS_TO_TICKS(100))==pdTRUE) {
            if(claimed && !cut_attempted && (interrupt_pending || uint32_t(now-gpio_at)>=(driver.state.expander_connected?50U:1000U))) {
                interrupt_pending=false;gpio_at=now;
                if(driver.poll_gpio()) {
                    if(driver.state.button_low && !button_held)sk_power_button_event();
                    button_held=driver.state.button_low;
                }
            }
            if(claimed && !cut_attempted && uint32_t(now-meter_at)>=(driver.state.meter_connected?100U:1000U)) {
                meter_at=now;driver.poll_meter();
            }
            publish();xSemaphoreGive(guard);
        }
        if(uint32_t(now-pd_at)>=1000) {
            pd_at=now;SkPowerPdSnapshot pd{};sk_pd_board_snapshot(&pd);
            taskENTER_CRITICAL();published_pd=pd;pd_published_at=millis();taskEXIT_CRITICAL();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
bool save_limit(uint16_t ma) {
#if SK_SETTINGS
    nvs_handle_t h;
    if(nvs_open("power_path",NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t e=nvs_set_u16(h,"max_ma",ma);
    if(e==ESP_OK)e=nvs_commit(h);
    nvs_close(h);return e==ESP_OK;
#else
    (void)ma;return false;
#endif
}
}
void sk_power_path_begin() {
    if(guard)return;
    guard=xSemaphoreCreateMutexStatic(&guard_storage);configASSERT(guard);
    const solar_os_resource_request_t req[]={
        {SOLAR_OS_RESOURCE_I2C_ADDRESS,bus,SK_POWER_MCP_ADDRESS,"MCP23008 power GPIO"},
        {SOLAR_OS_RESOURCE_I2C_ADDRESS,bus,SK_POWER_INA_ADDRESS,"INA237 10mOhm shunt"}};
    claimed=solar_os_resource_claim_bundle(req,2,"power-path",nullptr)==ESP_OK;
    uint16_t ma=0;
#if SK_SETTINGS
    nvs_handle_t h;
    if(nvs_open("power_path",NVS_READONLY,&h)==ESP_OK) {
        if(nvs_get_u16(h,"max_ma",&ma)!=ESP_OK)ma=0;
        nvs_close(h);
    }
#endif
    driver.set_limit(ma); // Cached until the physical INA237 is detected.
    pinMode(interrupt_pin,INPUT_PULLUP);
    attachInterrupt(interrupt_pin,interrupt,FALLING);
    configASSERT(xTaskCreateStatic(run,"power-path",1024,nullptr,2,stack,&tcb));
}
void sk_power_path_snapshot(SkPowerPathSnapshot *out) {
    taskENTER_CRITICAL();*out=published;const uint32_t at=published_at;taskEXIT_CRITICAL();
    if(uint32_t(millis()-at)>1500) {
        out->expander_connected=out->meter_connected=out->meter_valid=out->limit_verified=false;
    }
}
bool sk_power_path_usb(const char *operation) {
    if(!guard || !claimed || cut_attempted || (sk_power_requested() && !strcmp(operation,"on")) ||
       xSemaphoreTake(guard,pdMS_TO_TICKS(1000))!=pdTRUE)return false;
    if(cut_attempted || (sk_power_requested() && !strcmp(operation,"on"))) {xSemaphoreGive(guard);return false;}
    const bool ok=driver.usb(operation);publish();xSemaphoreGive(guard);return ok;
}
bool sk_power_path_usb_off() {return sk_power_path_usb("off");}
bool sk_power_path_cut() {
    if(!guard || !claimed || xSemaphoreTake(guard,pdMS_TO_TICKS(1000))!=pdTRUE)return false;
    if(cut_attempted) {xSemaphoreGive(guard);return false;}
    cut_attempted=true; // Never reinitialize GP6 back to input after asserting KILL.
    const bool ok=driver.cut();publish();xSemaphoreGive(guard);return ok;
}
void sk_power_path_lines(char lines[9][27]) {
    SkPowerPathSnapshot s{};sk_power_path_snapshot(&s);
    SkPowerPdSnapshot pd{};
    taskENTER_CRITICAL();pd=published_pd;const uint32_t pd_at=pd_published_at;taskEXIT_CRITICAL();
    if(uint32_t(millis()-pd_at)>2500)pd={};
    sk_power_path_format(s,pd,lines);
}
extern "C" void sk_shell_cmd_powerpath(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);io->command_status=1;
    if(argc==1 || (argc==2 && !strcmp(argv[1],"status"))) {
        SkPowerPathSnapshot s;sk_power_path_snapshot(&s);
        char lines[9][27]{};sk_power_path_lines(lines);
        solar_os_shell_io_printf(io,"Power path i2c1: MCP23008=0x%02x INA237=0x%02x STUSB4500=0x%02x; shunt=10 mOhm nominal; resources=%s\n",
            SK_POWER_MCP_ADDRESS,SK_POWER_INA_ADDRESS,SK_POWER_PD_ADDRESS,claimed?"claimed":"unavailable");
        for(unsigned i=1;i<8;i++)solar_os_shell_io_writeln(io,lines[i]);
        solar_os_shell_io_printf(io,"Limit desired=%u mA verified=%u; USB EN(command)=%u FAULT=%u latched=%u SINK=%u; KILL_N=%s INT_N=%s\n",
            s.limit_ma,s.limit_verified,s.usb_enabled,s.usb_fault,s.usb_latched,s.usb_attached,
            s.expander_connected?(s.kill_low?"low":"high"):"unknown",
            s.expander_connected?(s.button_low?"low":"high"):"unknown");
        solar_os_shell_io_writeln(io,"PD set = requested controller voltage / RDO current; USB EN/SINK are not measured host VBUS. Input priority PD > DC > AUX.");
        io->command_status=0;return;
    }
    if(argc==3 && !strcmp(argv[1],"max")) {
        unsigned long value=0;
        if(strcmp(argv[2],"off")) {
            if(!*argv[2])goto usage;
            for(const char *p=argv[2];*p;p++) {
                if(*p<'0' || *p>'9' || value>SK_POWER_MAX_CURRENT_MA)goto usage;
                value=value*10+unsigned(*p-'0');
            }
            if(value<100 || value>SK_POWER_MAX_CURRENT_MA || value>16383)goto usage;
        }
        if(!guard || xSemaphoreTake(guard,pdMS_TO_TICKS(1000))!=pdTRUE)return;
        // Commit first: a newly lower threshold can cut power during the write.
        const bool saved=save_limit(value);
        const bool applied=driver.set_limit(value);
        publish();xSemaphoreGive(guard);
        solar_os_shell_io_printf(io,"Max current %lu mA: %s; %s.\n",value,
            applied?(value?"hardware threshold armed":"custom threshold off"):"pending: hardware unconnected or write/readback failed",
            saved?"saved":"NOT saved (this boot only)");
        solar_os_shell_io_writeln(io,"INA237 ALERT drives LTC2954 KILL directly: overcurrent cuts power without filesystem cleanup. Max off restores the ADC full-scale threshold.");
        io->command_status=saved?0:1;return;
    }
usage:
    solar_os_shell_io_printf(io,"usage: powerpath status | powerpath max 100..%u (mA) | powerpath max off\n",SK_POWER_MAX_CURRENT_MA);
}
#endif
