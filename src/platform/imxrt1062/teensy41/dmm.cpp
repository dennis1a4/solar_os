#if SK_DMM
#include <arduino_freertos.h>
#include <semphr.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "dmm.h"
#include "dmm_model.h"
#include "small_display.h"
#include "adc_guard.h"
#include "power_shutdown.h"
extern "C" {
#include "solar_os.h"
#include "solar_os_shell_io.h"
#include "solar_os_resources.h"
}
#ifndef SK_DMM_ADC_PIN
#define SK_DMM_ADC_PIN 40
#endif
// Set only after validating both range dividers/protection and actual wiring.
#ifndef SK_DMM_INPUT_VERIFIED
#define SK_DMM_INPUT_VERIFIED 0
#endif
#ifndef SK_DMM_5V_FULL_SCALE_MV
#define SK_DMM_5V_FULL_SCALE_MV 5000
#endif
#ifndef SK_DMM_50V_FULL_SCALE_MV
#define SK_DMM_50V_FULL_SCALE_MV 50000
#endif
DMAMEM static DmmModel meter;
DMAMEM static StackType_t stack[2048] __attribute__((aligned(8)));
static StaticTask_t tcb;
static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t mutex;
static TaskHandle_t task;
static bool active,stopping,demo,claimed;
static uint32_t read_errors,display_misses;
static void lock() {xSemaphoreTake(mutex,portMAX_DELAY);}
static void unlock() {xSemaphoreGive(mutex);}

static bool live_supported() {
#if SK_SCOPE && SK_SCOPE_ADC_PIN >= 0
    // The DMA scope reconfigures both ADC controllers; pin locking is not enough.
    return false;
#else
    return SK_DMM_INPUT_VERIFIED && ((SK_DMM_ADC_PIN>=14 && SK_DMM_ADC_PIN<=27) || (SK_DMM_ADC_PIN>=38 && SK_DMM_ADC_PIN<=41)) &&
        SK_DMM_ADC_PIN!=SK_AMPLIFIER_SHUTDOWN_PIN;
#endif
}
static void run(void *) {
    DmmModel view;
    uint8_t previous=0;
    bool initialized=false;
    uint32_t rendered=0,sampled=0;
    for(;;) {
        lock();const bool running=active,stop=stopping;unlock();
        if(!running) {initialized=false;ulTaskNotifyTake(pdTRUE,portMAX_DELAY);continue;}
        if(stop || sk_power_cleaning()) {
            // Only this worker releases the screen/pin; no frame can follow it.
            if(claimed) {
                if(sk_adc_lock(50)) {pinMode(SK_DMM_ADC_PIN,INPUT);sk_adc_unlock();}
                solar_os_resource_release_owner("dmm");claimed=false;
            }
            sk_small_release();
            lock();active=false;stopping=false;unlock();
            initialized=false;continue;
        }
        sk_small_input_poll();
        const uint8_t held=sk_small_buttons();
        if(!initialized) {previous=held;rendered=millis()-500;sampled=millis()-50;initialized=true;}
        const uint8_t pressed=held & ~previous;previous=held;
        if(pressed)rendered=millis()-500;
        lock();
        if(!meter.buttons(pressed))stopping=true;
        const unsigned range=meter.range;
        const bool simulated=demo,stop_now=stopping;
        unlock();
        if(stop_now)continue;
        unsigned raw=0;bool ok=true;
        const uint32_t now=millis();
        if(uint32_t(now-sampled)<50) {vTaskDelay(pdMS_TO_TICKS(10));continue;}
        sampled=now;
        if(simulated) {
            // Slow triangle plus periodic step, explicitly marked DEMO.
            const unsigned phase=now%6000;
            raw=250+(phase<3000?phase:6000-phase)/8+((now/2000)%2)*100;
        } else if(sk_adc_lock(10)) {
            for(unsigned i=0;i<8;++i)raw+=analogRead(SK_DMM_ADC_PIN);
            raw=(raw+4)/8;sk_adc_unlock();
        } else ok=false;
        lock();
        if(ok && meter.range==range)meter.sample(now,dmm_scale(raw,range==5?SK_DMM_5V_FULL_SCALE_MV:SK_DMM_50V_FULL_SCALE_MV),raw>=1022);
        else ++read_errors;
        const bool draw=uint32_t(now-rendered)>=500;
        if(draw)view=meter;
        unlock();
        if(draw) {
            if(!sk_small_dmm_frame(&view,simulated)) {lock();++display_misses;unlock();}
            rendered=now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
static bool init() {
    // Shell commands are serialized by console_gate.
    if(task)return true;
    meter=DmmModel{};
    mutex=xSemaphoreCreateMutexStatic(&mutex_storage);
    if(!mutex)return false;
    task=xTaskCreateStatic(run,"dmm",2048,nullptr,1,stack,&tcb);
    return task!=nullptr;
}
extern "C" bool sk_dmm_stop() {
    if(!task)return true;
    lock();const bool was_active=active;if(was_active)stopping=true;unlock();
    if(!was_active)return true;
    xTaskNotifyGive(task);
    const uint32_t began=millis();
    while(uint32_t(millis()-began)<1000) {
        lock();const bool done=!active;unlock();if(done)return true;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}
static void status(solar_os_shell_io_t *io) {
    lock();
    const bool running=active,simulated=demo;
    const unsigned range=meter.range,mode=meter.mode;
    const bool held=meter.hold,valid=meter.valid,overload=meter.displayed_overload();
    const int32_t value=meter.displayed(),lo=meter.minimum,hi=meter.maximum;
    const uint32_t samples=meter.samples,missed=meter.missed,errors=read_errors,misses=display_misses;
    unlock();
    solar_os_shell_io_printf(io,"DMM %s source=%s pin=%d range=%uV mode=%s hold=%s valid=%s overload=%s value-mV=%ld min-mV=%ld max-mV=%ld samples=%lu missed=%lu read-errors=%lu display-misses=%lu\n",
        running?"running":"stopped",simulated?"DEMO":"ADC",SK_DMM_ADC_PIN,range,
        mode==DmmModel::Minimum?"MIN":mode==DmmModel::Maximum?"MAX":"LIVE",held?"on":"off",valid?"yes":"no",overload?"yes":"no",
        (long)value,(long)lo,(long)hi,(unsigned long)samples,(unsigned long)missed,(unsigned long)errors,(unsigned long)misses);
}
extern "C" void solar_os_shell_cmd_dmm(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);io->command_status=0;
    if(!init()) {io->command_status=1;solar_os_shell_io_writeln(io,"dmm: task unavailable");return;}
    const char *command=argc>1?argv[1]:"start";
    if(!strcmp(command,"status") && argc==2) {status(io);return;}
    if(!strcmp(command,"stop") && argc==2) {
        io->command_status=sk_dmm_stop()?0:1;
        solar_os_shell_io_writeln(io,io->command_status?"dmm: stop pending":"DMM stopped; small display restored");return;
    }
    if(argc==2 && (!strcmp(command,"hold") || !strcmp(command,"min") || !strcmp(command,"max") || !strcmp(command,"live") || !strcmp(command,"reset"))) {
        lock();
        if(!strcmp(command,"hold"))meter.toggle_hold();
        else if(!strcmp(command,"reset"))meter.reset_extrema();
        else meter.set_mode(!strcmp(command,"min")?DmmModel::Minimum:!strcmp(command,"max")?DmmModel::Maximum:DmmModel::Live);
        unlock();status(io);return;
    }
    if(argc==3 && !strcmp(command,"range") && (!strcmp(argv[2],"5") || !strcmp(argv[2],"50"))) {
        lock();meter.set_range(unsigned(atoi(argv[2])));unlock();status(io);return;
    }
    bool simulated=!strcmp(command,"--demo"),valid=!strcmp(command,"start") || simulated;
    unsigned range=5;
    for(int i=2;i<argc && valid;++i) {
        if(!strcmp(argv[i],"--demo"))simulated=true;
        else if(!strcmp(argv[i],"--range") && i+1<argc && (!strcmp(argv[i+1],"5") || !strcmp(argv[i+1],"50")))range=unsigned(atoi(argv[++i]));
        else valid=false;
    }
    if(!valid) {
        io->command_status=2;
        solar_os_shell_io_writeln(io,"usage: dmm [start|--demo] [--demo] [--range 5|50] | status|stop|hold|min|max|live|reset | range 5|50");return;
    }
    lock();const bool running=active;unlock();
    if(running) {status(io);return;}
    if(sk_power_requested()) {io->command_status=1;solar_os_shell_io_writeln(io,"dmm: shutdown pending");return;}
    if(!simulated && !live_supported()) {
        io->command_status=1;
        solar_os_shell_io_writeln(io,"dmm: live input disabled; verify 5V/50V dividers/protection and free ADC40 from amplifier/scope ownership. Use dmm --demo.");return;
    }
    if(!simulated) {
        if(solar_os_resource_claim(SOLAR_OS_RESOURCE_GPIO_PIN,SK_DMM_ADC_PIN,-1,"dmm","DMM probe")!=ESP_OK) {
            io->command_status=1;solar_os_shell_io_writeln(io,"dmm: probe pin is already claimed");return;
        }
        claimed=true;
        if(!sk_adc_lock(50)) {solar_os_resource_release_owner("dmm");claimed=false;io->command_status=1;return;}
        pinMode(SK_DMM_ADC_PIN,INPUT);sk_adc_unlock();
    }
    if(!sk_small_acquire()) {
        if(claimed) {solar_os_resource_release_owner("dmm");claimed=false;}
        io->command_status=1;solar_os_shell_io_writeln(io,"dmm: small display busy or unavailable");return;
    }
    lock();meter=DmmModel{};meter.set_range(range);demo=simulated;read_errors=display_misses=0;stopping=false;active=true;unlock();
    xTaskNotifyGive(task);
    solar_os_shell_io_writeln(io,"DMM running in background. NES: A hold, B live/min/max, Left min, Right max, Up 5V, Down 50V, Select reset, Start stop.");
    status(io);
}
#endif
