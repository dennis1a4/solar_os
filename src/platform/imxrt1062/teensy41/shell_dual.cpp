#if SK_UPSTREAM_SHELL && SK_LCD_CONSOLE
#include <arduino_freertos.h>
#include <semphr.h>
#include "platform.h"
extern "C" {
#include "solar_os.h"
#include "solar_os_shell.h"
#include "solar_os_shell_io.h"
#include "solar_os_app_registry.h"
#include "solar_os_port.h"
#include "solar_os_keys.h"
#include "solar_os_vt100.h"
#include "solar_os_sessions.h"
#include "solar_os_memory.h"
#include "solar_os_tui.h"
#if SK_SETTINGS
#include "nvs.h"
#endif
}

struct AppFrame;
struct Console {
    solar_os_context_t context;
    solar_os_shell_session_t *shell;
    const solar_os_app_t *app;
    solar_os_tui_t *tui;
    AppFrame *frame;
    solar_os_port_handle_t port;
    const char *name;
    bool local;
};
static Console consoles[2];
static TaskHandle_t local_task;
static SemaphoreHandle_t console_gate;
static volatile bool audio_local;
DMAMEM static StackType_t local_stack[10240];
static StaticTask_t local_tcb;
extern bool sk_lcd_ready();
extern void sk_lcd_write(const char *,size_t);
extern void sk_lcd_flush();
extern void sk_lcd_row(unsigned,char *);
extern void sk_usb_inject(const char *);
extern bool sk_usb_keyboard_connected();
extern void sk_usb_status(char *,size_t);
static Console &active() { return consoles[xTaskGetCurrentTaskHandle()==local_task ? 1 : 0]; }
#define shell_context (active().context)
#define session (active().shell)
#define foreground (active().app)
#define active_tui (active().tui)
#define owner (active().name)
#define app_frame (active().frame)
extern "C" bool sk_console_is_local() { return active().local; }
extern "C" bool sk_audio_owner_connected() { return audio_local || bool(Serial); }
static bool connected() { return active().local || bool(Serial); }
// Application lifecycle and filesystem calls are serialized. At safe polling
// boundaries, a synchronous interpreter/player yields the gate to the other
// console. No second task enters a filesystem operation midway through one.
static void console_yield() {
    sk_lcd_flush();
    xSemaphoreGiveRecursive(console_gate);
    vTaskDelay(pdMS_TO_TICKS(2));
    xSemaphoreTakeRecursive(console_gate,portMAX_DELAY);
}
static int read_key() {
    if(active().local) { sk_usb_poll(); return sk_usb_read(); }
    return Serial.available()?Serial.read():-1;
}
extern "C" bool sk_console_poll_cancel(bool escape) {
    if(!connected()) return true;
    bool stop=false;
    for(int ch;(ch=read_key())>=0;) stop|=ch==3 || ch==29 || (escape && ch==27);
    console_yield();
    return stop;
}
extern "C" bool sk_python_poll_cancel() { return sk_console_poll_cancel(false); }
extern "C" void sk_console_delay_ms(uint32_t ms) {
    const uint32_t start=millis();
    while(millis()-start<ms) console_yield();
}
extern "C" void solar_os_sessions_attach_tui(solar_os_shell_io_t *io,solar_os_tui_t *tui) {
    configASSERT(session && io==solar_os_shell_session_io(session)); active_tui=tui;
}
extern "C" void solar_os_sessions_detach_tui(solar_os_shell_io_t *io,const solar_os_tui_t *tui) {
    if(session && io==solar_os_shell_session_io(session) && active_tui==tui) active_tui=nullptr;
}
extern "C" size_t solar_os_sessions_shell_count() { return 2; }
static esp_err_t terminal_write(void *user,const uint8_t *data,size_t length,size_t *written) {
    if(user) { sk_lcd_write(reinterpret_cast<const char *>(data),length); *written=length; }
    else *written=Serial?Serial.write(data,length):0;
    return *written==length?ESP_OK:ESP_ERR_TIMEOUT;
}
static esp_err_t terminal_read(void *,uint8_t *data,size_t length,uint32_t timeout,size_t *received) {
    const uint32_t start=millis(); *received=0;
    do {
        int ch;
        while(*received<length && (ch=read_key())>=0) data[(*received)++]=ch;
        if(*received || millis()-start>=timeout) return ESP_OK;
        console_yield();
    } while(true);
}
static bool audio_app(const solar_os_app_t *app) {
    return app && (!strcmp(app->name,"synth") || !strcmp(app->name,"aplay") || !strcmp(app->name,"arecord"));
}
static bool sk_app_allowed(const solar_os_app_t *app) {
    if(!audio_app(app)) return true;
    for(auto &console:consoles) {
        // Check retained parent apps too through the foreground reservation:
        // audio apps never launch children in this port.
        if(audio_app(console.app)) return false;
    }
    audio_local=active().local;
    return true;
}
extern "C" uint32_t sk_python_random_seed() { return micros() ^ ARM_DWT_CYCCNT; }
#include "shell_children.h"
static bool emit_key(char ch, void *) {
    solar_os_event_t event{};
    event.type = SOLAR_OS_EVENT_CHAR;
    event.data.ch = ch == 3 && (!foreground || !strcmp(foreground->name, "calc"))
        ? SOLAR_OS_KEY_ESCAPE : ch;
    if (foreground) {
        if (foreground->event) foreground->event(current_context(), &event);
        else if (static_cast<uint8_t>(ch) == SOLAR_OS_KEY_APP_EXIT) solar_os_context_finish(current_context(), 0, nullptr);
    } else solar_os_shell_session_event(&shell_context, session, &event);
    service_requests();
    return true;
}

static void run_console(void *) {
    xSemaphoreTakeRecursive(console_gate,portMAX_DELAY);
    session=solar_os_shell_session_create(); configASSERT(session);
    extern solar_os_gfx_t *sk_lcd_gfx();
    solar_os_context_init(&shell_context,nullptr,active().local ? sk_lcd_gfx() : nullptr);
    auto *io=solar_os_shell_session_io(session);
    solar_os_shell_io_init_port(io,&active().port,active().local?100:80,active().local?30:24);
    solar_os_shell_io_set_terminal_profile(io,SOLAR_OS_SHELL_TERMINAL_PROFILE_VT100);
    if(active().local) solar_os_shell_io_set_charset(io,SOLAR_OS_SHELL_CHARSET_ASCII);
#if SK_SETTINGS
    if(!active().local) {
        nvs_handle_t h;
        if(nvs_open("usb_terminal",NVS_READONLY,&h)==ESP_OK) {
            uint16_t cols=80,rows=24;
            if(nvs_get_u16(h,"cols",&cols)==ESP_OK && nvs_get_u16(h,"rows",&rows)==ESP_OK &&
               cols>=20 && cols<=300 && rows>=8 && rows<=120) solar_os_shell_io_set_dimensions(io,cols,rows);
            nvs_close(h);
        }
    }
#endif
    solar_os_vt100_input_t input; solar_os_vt100_input_init(&input);
    bool online=false,was_cr=false;
    uint32_t last_byte=0,last_tick=0;
    while(true) {
        if(foreground && foreground->event && millis()-last_tick>=solar_os_app_tick_interval_ms(foreground,25)) {
            last_tick=millis(); solar_os_event_t event{}; event.type=SOLAR_OS_EVENT_TICK; event.data.tick_ms=millis();
            foreground->event(current_context(),&event); service_requests();
        }
        if(connected() && !online) {
            online=true; solar_os_vt100_input_reset(&input); was_cr=false;
            while(foreground) { solar_os_context_finish(current_context(),0,nullptr); service_requests(); }
            // Startup belongs to the always-present local terminal, once per boot.
            solar_os_shell_session_start(&shell_context,session,io,false,active().local && SK_SETTINGS);
            service_requests();
        } else if(!connected()) online=false;
        int ch=online?read_key():-1;
        if(ch>=0) {
            last_byte=millis();
            if(ch=='\n' && was_cr) was_cr=false;
            else { was_cr=ch=='\r'; solar_os_vt100_input_feed_byte(&input,ch,emit_key,nullptr); }
        } else if(solar_os_vt100_input_pending(&input) && millis()-last_byte>=40)
            solar_os_vt100_input_flush(&input,emit_key,nullptr);
        console_yield();
    }
}
extern "C" void solar_os_shell_cmd_lcd(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if(argc==2 && !strcmp(argv[1],"dump")) {
        if(active().local) { solar_os_shell_io_writeln(io,"Use lcd dump from USB."); return; }
        char row[101];
        for(unsigned r=0;r<30;++r) { sk_lcd_row(r,row); solar_os_shell_io_writeln(io,row); }
    } else if(argc==3 && !strcmp(argv[1],"send") && !active().local) {
        if(strlen(argv[2])>200) { solar_os_shell_io_writeln(io,"Command too long."); return; }
        sk_usb_inject(argv[2]); sk_usb_inject("\r");
        solar_os_shell_io_writeln(io,"Queued for LCD session.");
    } else if(argc==3 && !strcmp(argv[1],"key") && !active().local) {
        if(!strcmp(argv[2],"exit")) sk_usb_inject("\035");
        else if(!strcmp(argv[2],"ctrlc")) sk_usb_inject("\003");
        else if(!strcmp(argv[2],"esc")) sk_usb_inject("\033");
        else { solar_os_shell_io_writeln(io,"usage: lcd key exit|ctrlc|esc"); return; }
        solar_os_shell_io_writeln(io,"Queued for LCD session.");
    } else {
        solar_os_shell_io_printf(io,"LCD 100x30; two consoles: usb and lcd; current=%s; keyboard=%s\n",
            owner,sk_usb_keyboard_connected()?"connected":"absent");
        char status[160]; sk_usb_status(status,sizeof(status)); solar_os_shell_io_writeln(io,status);
#if SK_PLOT
        extern void sk_gfx_status(char *,size_t); sk_gfx_status(status,sizeof(status)); solar_os_shell_io_writeln(io,status);
#endif
    }
}
void sk_upstream_shell_run() {
#if SK_PLOT
    extern void sk_plot_streams_begin(); sk_plot_streams_begin();
#endif
    configASSERT(sk_lcd_ready());
    console_gate=xSemaphoreCreateRecursiveMutex(); configASSERT(console_gate);
    const solar_os_port_driver_t usb={"usb","Teensy USB CDC",SOLAR_OS_PORT_CAP_READ|SOLAR_OS_PORT_CAP_WRITE,
        terminal_read,terminal_write,nullptr,nullptr,nullptr};
    const solar_os_port_driver_t display={"lcd","RA8875 and USB host keyboard",SOLAR_OS_PORT_CAP_READ|SOLAR_OS_PORT_CAP_WRITE,
        terminal_read,terminal_write,nullptr,nullptr,reinterpret_cast<void *>(1)};
    configASSERT(solar_os_port_register(&usb)==ESP_OK && solar_os_port_register(&display)==ESP_OK);
    consoles[0].name="usb-shell"; consoles[1].name="lcd-shell"; consoles[1].local=true;
    configASSERT(solar_os_port_claim("usb","usb-shell",&consoles[0].port)==ESP_OK);
    configASSERT(solar_os_port_claim("lcd","lcd-shell",&consoles[1].port)==ESP_OK);
    // Hold gate until the local task handle is published.
    xSemaphoreTakeRecursive(console_gate,portMAX_DELAY);
    local_task=xTaskCreateStatic(run_console,"lcd-console",10240,nullptr,2,local_stack,&local_tcb);
    configASSERT(local_task);
    xSemaphoreGiveRecursive(console_gate);
    run_console(nullptr);
}
#endif
