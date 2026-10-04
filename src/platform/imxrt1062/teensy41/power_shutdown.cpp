#if SK_UPSTREAM_SHELL && SK_HW_RESOURCES && SK_LCD_CONSOLE
#include <arduino_freertos.h>
#include "platform.h"
#include "power_shutdown.h"
#include "serial_terminal.h"
extern "C" {
#include "solar_os.h"
#include "solar_os_shell_io.h"
int sk_shutdown_preflight(void);
bool sk_shutdown_ready(void);
bool sk_storage_shutdown_sync(void);
#if SK_POWER
bool sk_pd_shutdown(void);
#endif
}
// ISR only latches the event; all cleanup runs in task context. Keep the
// coordinator stack internal so a blocked VM cannot prevent timeout handling.
static volatile bool button_pending, pending, cleaning;
static volatile uint32_t generation, began;
static const char *volatile status="idle";
static bool check_only;
DMAMEM static StackType_t stack[2048] __attribute__((aligned(8)));
static StaticTask_t tcb;
extern "C" bool sk_power_requested() { return pending; }
extern "C" bool sk_power_cleaning() { return cleaning; }
extern "C" uint32_t sk_power_generation() { return generation; }
extern "C" bool sk_power_interrupt_due() { return cleaning && uint32_t(millis()-began)>=2000; }
extern "C" const char *sk_power_status() { return status; }
static bool expired() { return uint32_t(millis()-began)>=15000; }
static void finish(const char *message) { status=message;cleaning=false;pending=false; }
static bool request(bool check) {
    taskENTER_CRITICAL();
    if(pending) {taskEXIT_CRITICAL();return false;}
    check_only=check;began=millis();++generation;status="waiting for applications";pending=true;
    taskEXIT_CRITICAL();return true;
}
extern "C" void sk_power_button_event() { if(!pending)button_pending=true; }
extern "C" void sk_power_button_init();
extern "C" void sk_power_cut();
static void shutdown_step() {
    if(button_pending) {button_pending=false;request(false);}
    if(pending) {
        if(expired())finish("timeout; power remains on (inspect Python/drivers)");
        else if(!cleaning) {
            int result=sk_shutdown_preflight();
            if(result<0)finish("blocked: close active/retained apps before shutdown");
            else if(result>0) {status="waiting for Python and console cleanup";cleaning=true;}
        } else if(sk_shutdown_ready()) {
            status="draining serial logs";
            if(sk_serial_shutdown()!=ESP_OK)finish("serial log error; power remains on");
            else {
                status="syncing storage";
                if(!sk_storage_shutdown_sync())finish("storage busy or sync failed; power remains on");
#if SK_POWER
                else if(!sk_pd_shutdown())finish("cannot confirm USB-PD 5 V; power remains on");
#endif
                else if(expired())finish("cleanup exceeded deadline; power remains on");
                else if(check_only)finish("check complete; power remains on (jobs stopped)");
                else {
                    status="powering off";
                    sk_power_cut();
                }
            }
        }
    }
}

static void run(void *) {
    for(;;) {shutdown_step();vTaskDelay(pdMS_TO_TICKS(10));}
}
extern "C" void sk_power_init() {
    configASSERT(xTaskCreateStatic(run,"shutdown",2048,nullptr,1,stack,&tcb));
    sk_power_button_init();
}
extern "C" void sk_shell_cmd_poweroff(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);io->command_status=1;
    if(argc==2 && !strcmp(argv[1],"status")) {solar_os_shell_io_printf(io,"poweroff: %s\n",sk_power_status());io->command_status=0;return;}
    if(argc>2 || (argc==2 && strcmp(argv[1],"--check"))) {solar_os_shell_io_writeln(io,"usage: poweroff [--check|status]");return;}
    if(request(argc==2)) {solar_os_shell_io_writeln(io,"Shutdown requested; use poweroff status if power stays on.");io->command_status=0;}
    else solar_os_shell_io_writeln(io,"Shutdown already pending.");
}
#endif
