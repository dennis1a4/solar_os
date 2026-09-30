#if SK_UPSTREAM_SHELL && SK_LCD_CONSOLE
#include <arduino_freertos.h>
#include <semphr.h>
#include "platform.h"
#if SK_TELNETD
#include "telnet_console.h"
#endif
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
#include "solar_os_storage.h"
#include "solar_os_tui.h"
#if SK_CLOCK
#include "solar_os_schedule.h"
#endif
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
static Console *audio_owner;
#if SK_TELNETD
DMAMEM static Console remote_console;
DMAMEM static StackType_t remote_stack[10240];
static StaticTask_t remote_tcb;
static TaskHandle_t remote_task;
#endif
DMAMEM static StackType_t local_stack[10240];
static StaticTask_t local_tcb;
extern bool sk_lcd_ready();
extern void sk_lcd_write(const char *,size_t);
extern void sk_lcd_flush();
extern void sk_lcd_row(unsigned,char *);
extern void sk_usb_inject(const char *);
extern bool sk_usb_keyboard_connected();
extern void sk_usb_status(char *,size_t);
extern void sk_usb_input_boundary();
extern uint32_t sk_usb_input_generation();
extern bool sk_usb_last_read_physical();
static Console &active() {
#if SK_TELNETD
    if(remote_task && xTaskGetCurrentTaskHandle()==remote_task)return remote_console;
#endif
    return consoles[xTaskGetCurrentTaskHandle()==local_task ? 1 : 0];
}
#if SK_TELNETD
extern "C" bool sk_console_is_remote() { return &active()==&remote_console; }
#endif
#define shell_context (active().context)
#define session (active().shell)
#define foreground (active().app)
#define active_tui (active().tui)
#define owner (active().name)
#define app_frame (active().frame)
extern "C" bool sk_console_is_local() { return active().local; }
extern "C" void sk_console_input_boundary() { if(active().local)sk_usb_input_boundary(); }
extern "C" bool sk_audio_owner_connected() {
#if SK_TELNETD
    if(audio_owner==&remote_console)return sk_telnet_connected();
#endif
    return (audio_owner && audio_owner->local) || bool(Serial);
}
static bool connected() {
#if SK_TELNETD
    if(sk_console_is_remote()) { sk_telnet_poll(false); return sk_telnet_connected(); }
#endif
    return active().local || bool(Serial);
}
// Application lifecycle and filesystem calls are serialized. At safe polling
// boundaries, a synchronous interpreter/player yields the gate to the other
// console. No second task enters a filesystem operation midway through one.
static void console_yield() {
#if SK_CLOCK
    solar_os_schedule_poll();
#endif
    sk_lcd_flush();
    xSemaphoreGiveRecursive(console_gate);
    vTaskDelay(pdMS_TO_TICKS(2));
    xSemaphoreTakeRecursive(console_gate,portMAX_DELAY);
}
static int read_key() {
    if(active().local) { sk_usb_poll(); return sk_usb_read(); }
#if SK_TELNETD
    if(sk_console_is_remote())return sk_telnet_read();
#endif
    return Serial.available()?Serial.read():-1;
}
extern "C" bool sk_console_poll_cancel(bool escape) {
    if(!connected()) return true;
    bool stop=false;
    for(int ch;(ch=read_key())>=0;) {
        if(ch==3 || ch==29 || (escape && ch==27))stop=true;
#if SK_GRAPHICS
        else if(foreground && !strcmp(foreground->name,"python")) { extern void sk_python_gfx_key(int);sk_python_gfx_key(ch); }
#endif
    }
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
extern "C" size_t solar_os_sessions_shell_count() {
#if SK_TELNETD
    return 2+(remote_console.shell!=nullptr);
#else
    return 2;
#endif
}
static esp_err_t terminal_write(void *user,const uint8_t *data,size_t length,size_t *written) {
#if SK_TELNETD
    if(user==reinterpret_cast<void *>(2)) {
        *written=sk_telnet_write(data,length)?length:0;
        return *written==length?ESP_OK:ESP_ERR_TIMEOUT;
    }
#endif
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
#if SK_TELNETD
    if(audio_app(remote_console.app))return false;
#endif
    audio_owner=&active();
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

static bool initialize_console() {
    session=solar_os_shell_session_create(); if(!session)return false;
    extern solar_os_gfx_t *sk_lcd_gfx();
    solar_os_context_init(&shell_context,nullptr,active().local ? sk_lcd_gfx() : nullptr);
    auto *io=solar_os_shell_session_io(session);
    solar_os_shell_io_init_port(io,&active().port,active().local?100:80,active().local?30:24);
    solar_os_shell_io_set_terminal_profile(io,SOLAR_OS_SHELL_TERMINAL_PROFILE_VT100);
    if(active().local) solar_os_shell_io_set_charset(io,SOLAR_OS_SHELL_CHARSET_ASCII);
#if SK_SETTINGS
    if(!active().local
#if SK_TELNETD
       && !sk_console_is_remote()
#endif
    ) {
        nvs_handle_t h;
        if(nvs_open("usb_terminal",NVS_READONLY,&h)==ESP_OK) {
            uint16_t cols=80,rows=24;
            if(nvs_get_u16(h,"cols",&cols)==ESP_OK && nvs_get_u16(h,"rows",&rows)==ESP_OK &&
               cols>=20 && cols<=300 && rows>=8 && rows<=120) solar_os_shell_io_set_dimensions(io,cols,rows);
            nvs_close(h);
        }
    }
#endif
#if SK_TELNETD
    if(sk_console_is_remote()) {
        uint16_t cols,rows; sk_telnet_dimensions(&cols,&rows);
        solar_os_shell_io_set_dimensions(io,cols,rows);
    }
#endif
    return true;
}
static void run_console(void *) {
    xSemaphoreTakeRecursive(console_gate,portMAX_DELAY);
#if SK_TELNETD
    if(!sk_console_is_remote())
#endif
        configASSERT(initialize_console());
    solar_os_vt100_input_t input; solar_os_vt100_input_init(&input);
    uint32_t input_generation=active().local?sk_usb_input_generation():0;
    bool input_physical=false;
    bool online=false,was_cr=false;
    uint32_t last_byte=0,last_tick=0;
    while(true) {
#if SK_TELNETD
        if(sk_console_is_remote()) {
            sk_telnet_poll(!session);
            if(!sk_telnet_connected()) {
                // Drop the entire app chain without resuming retained parents.
                while(app_frame) {
                    auto *old=app_frame;
                    if(!foreground->state_slot || *foreground->state_slot)
                        solar_os_app_stop(foreground,current_context());
                    solar_os_app_registry_release(foreground,owner);
                    app_frame=old->parent; solar_os_memory_free(old); select_frame();
                }
                if(session) { solar_os_shell_session_destroy(session); session=nullptr; }
                memset(&shell_context,0,sizeof(shell_context)); active_tui=nullptr;
                online=false; solar_os_vt100_input_reset(&input);
                console_yield(); continue;
            }
            if(!session && !initialize_console()) { sk_telnet_disconnect(); console_yield(); continue; }
            uint16_t cols,rows; sk_telnet_dimensions(&cols,&rows);
            auto *remote_io=solar_os_shell_session_io(session);
            if(cols!=solar_os_shell_io_cols(remote_io) || rows!=solar_os_shell_io_rows(remote_io))
                solar_os_shell_io_set_dimensions(remote_io,cols,rows);
        }
#endif
        auto *io=solar_os_shell_session_io(session);
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
        if(online && !foreground && millis()-last_tick>=25) {
            last_tick=millis();
            solar_os_event_t tick{};
            tick.type=SOLAR_OS_EVENT_TICK; tick.data.tick_ms=pdTICKS_TO_MS(xTaskGetTickCount());
            solar_os_shell_session_event(&shell_context,session,&tick);
            service_requests();
        }
        int ch=online?read_key():-1;
        if(active().local) {
            const uint32_t current=sk_usb_input_generation();
            if(current!=input_generation) {
                if(input_physical) { solar_os_vt100_input_reset(&input); was_cr=false; }
                input_generation=current;
            }
            if(ch>=0)input_physical=sk_usb_last_read_physical();
        }
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
        else if(!strcmp(argv[2],"up")) sk_usb_inject("\033[A");
        else if(!strcmp(argv[2],"down")) sk_usb_inject("\033[B");
        else if(!strcmp(argv[2],"right")) sk_usb_inject("\033[C");
        else if(!strcmp(argv[2],"left")) sk_usb_inject("\033[D");
        else if(!strcmp(argv[2],"home")) sk_usb_inject("\033[H");
        else if(!strcmp(argv[2],"end")) sk_usb_inject("\033[F");
        else if(!strcmp(argv[2],"tab")) sk_usb_inject("\t");
        else if(!strcmp(argv[2],"space")) sk_usb_inject(" ");
        else if(!strcmp(argv[2],"enter")) sk_usb_inject("\r");
        else { solar_os_shell_io_writeln(io,"usage: lcd key exit|ctrlc|esc|up|down|left|right|home|end|tab|space|enter"); return; }
        solar_os_shell_io_writeln(io,"Queued for LCD session.");
    } else {
        solar_os_shell_io_printf(io,"LCD 100x30; local consoles: usb and lcd; current=%s; keyboard=%s\n",
            owner,sk_usb_keyboard_connected()?"connected":"absent");
        char status[160]; sk_usb_status(status,sizeof(status)); solar_os_shell_io_writeln(io,status);
#if SK_PLOT
        extern void sk_gfx_status(char *,size_t); sk_gfx_status(status,sizeof(status)); solar_os_shell_io_writeln(io,status);
#endif
    }
}
extern "C" void sk_shell_cmd_session(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if(argc>2 || (argc==2 && strcmp(argv[1],"list"))) {
        solar_os_shell_io_writeln(io,"usage: session [list] | sessions");
        solar_os_shell_io_writeln(io,"This port has fixed USB/LCD/Telnet consoles; detached sessions and fg are not yet integrated.");
        return;
    }
    solar_os_shell_io_writeln(io,"ID Owner          Connected Foreground       Directory");
    Console *entries[]={&consoles[0],&consoles[1],
#if SK_TELNETD
        &remote_console,
#endif
    };
    for(unsigned i=0;i<sizeof(entries)/sizeof(entries[0]);++i) {
        Console &c=*entries[i];
        char path[SOLAR_OS_STORAGE_PATH_MAX]="-";
        if(c.shell)solar_os_shell_resolve_path(&c.context,nullptr,path,sizeof(path));
        bool online=c.local || (i==0 && bool(Serial));
#if SK_TELNETD
        if(i==2)online=sk_telnet_connected();
#endif
        solar_os_shell_io_printf(io,"%u  %-14s %-9s %-16s %s%s\n",i+1,c.name,
            online?"yes":"no",c.app?c.app->name:"shell",path,&c==&active()?" *":"");
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
#if SK_TELNETD
    sk_telnet_init(); memset(&remote_console,0,sizeof(remote_console));
    const solar_os_port_driver_t remote={"telnet0","Telnet authenticated shell",SOLAR_OS_PORT_CAP_READ|SOLAR_OS_PORT_CAP_WRITE,
        terminal_read,terminal_write,nullptr,nullptr,reinterpret_cast<void *>(2)};
    configASSERT(solar_os_port_register(&remote)==ESP_OK);
    remote_console.name="telnet-shell";
    configASSERT(solar_os_port_claim("telnet0",remote_console.name,&remote_console.port)==ESP_OK);
#endif
    // Hold gate until task handles are published.
    xSemaphoreTakeRecursive(console_gate,portMAX_DELAY);
    local_task=xTaskCreateStatic(run_console,"lcd-console",10240,nullptr,2,local_stack,&local_tcb);
    configASSERT(local_task);
#if SK_TELNETD
    remote_task=xTaskCreateStatic(run_console,"telnet-console",10240,nullptr,2,remote_stack,&remote_tcb);
    configASSERT(remote_task);
#endif
    xSemaphoreGiveRecursive(console_gate);
    run_console(nullptr);
}
#endif
