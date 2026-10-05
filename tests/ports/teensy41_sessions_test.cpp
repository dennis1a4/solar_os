#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#define SK_LCD_CONSOLE 1
extern "C" {
#include "solar_os.h"
#include "solar_os_shell.h"
#include "solar_os_app_registry.h"
#include "solar_os_memory.h"
#include "solar_os_tui.h"
#include "solar_os_storage.h"
}
struct AppFrame;
struct Console {
 solar_os_context_t context{};solar_os_shell_session_t *shell{};
 const solar_os_app_t *app{};solar_os_tui_t *tui{};AppFrame *frame{},*retained[4]{};
 uint32_t request_id{};uint8_t request_action{};bool quiet{},dispatching{};const char *name{};bool local{};
};
static Console consoles[2];static unsigned current;static bool Serial=true;
static Console &active(){return consoles[current];}
#define shell_context (active().context)
#define session (active().shell)
#define foreground (active().app)
#define active_tui (active().tui)
#define app_frame (active().frame)
#define owner (active().name)
static uint32_t next_app_id=4;
static solar_os_shell_io_t io;
static unsigned allocations,frees,starts,stops,resumes,suspends,prompts,boundaries;
static bool fail_alloc,fail_start;
static std::string output;
static const solar_os_app_t *claims[32]{};
static void sk_console_input_boundary(){++boundaries;}
static void session_text_mode(){}
static bool shell_idle=true;
extern "C" bool solar_os_shell_session_is_idle(const solar_os_shell_session_t *){return shell_idle;}
static bool sk_app_allowed(const solar_os_app_t *){return true;}
static bool audio_app(const solar_os_app_t *app){return app && !strcmp(app->name,"synth");}
extern "C" size_t strlcpy(char *out,const char *in,size_t n){size_t len=strlen(in);if(n){size_t k=len<n-1?len:n-1;memcpy(out,in,k);out[k]=0;}return len;}
extern "C" void *solar_os_memory_calloc(size_t n,size_t s,solar_os_memory_class_t,const char *){if(fail_alloc)return nullptr;++allocations;return calloc(n,s);}
extern "C" void solar_os_memory_free(void *p){if(p){++frees;free(p);}}
extern "C" esp_err_t solar_os_log_write(solar_os_log_level_t,const char *,const char *,...){return ESP_OK;}
extern "C" const char *esp_err_to_name(esp_err_t){return "error";}
extern "C" esp_err_t solar_os_app_registry_claim(const solar_os_app_t *app,const char *,char *,size_t){for(auto *p:claims)if(p==app)return ESP_ERR_INVALID_STATE;for(auto &p:claims)if(!p){p=app;return ESP_OK;}return ESP_FAIL;}
extern "C" void solar_os_app_registry_release(const solar_os_app_t *app,const char *){for(auto &p:claims)if(p==app){p=nullptr;return;}assert(false);}
extern "C" solar_os_shell_io_t *solar_os_shell_session_io(solar_os_shell_session_t *){return &io;}
extern "C" void solar_os_shell_session_set_foreground_app(solar_os_shell_session_t *,const solar_os_app_t *){}
extern "C" void solar_os_shell_session_set_exit_result(solar_os_shell_session_t *,int,const char *){}
extern "C" void solar_os_shell_session_prompt(solar_os_context_t *,solar_os_shell_session_t *){++prompts;}
extern "C" esp_err_t solar_os_shell_session_start(solar_os_context_t *,solar_os_shell_session_t *,solar_os_shell_io_t *,bool,bool){return ESP_OK;}
extern "C" esp_err_t solar_os_shell_resolve_path(solar_os_context_t *,const char *,char *out,size_t n){strlcpy(out,"/",n);return ESP_OK;}
extern "C" esp_err_t solar_os_shell_io_clear(solar_os_shell_io_t *){return ESP_OK;}
extern "C" esp_err_t solar_os_shell_io_write(solar_os_shell_io_t *,const char *s){if(!active().quiet)output+=s;return ESP_OK;}
extern "C" esp_err_t solar_os_shell_io_writeln(solar_os_shell_io_t *p,const char *s){return solar_os_shell_io_write(p,s);}
extern "C" esp_err_t solar_os_shell_io_printf(solar_os_shell_io_t *p,const char *fmt,...){char b[512];va_list args;va_start(args,fmt);vsnprintf(b,sizeof(b),fmt,args);va_end(args);return solar_os_shell_io_write(p,b);}
extern "C" void solar_os_shell_io_capture_output(solar_os_shell_io_t *,solar_os_context_t *){}
extern "C" void solar_os_tui_attach_session(solar_os_tui_t *tui){active_tui=tui;}
static bool suspend_app();
#include "platform/imxrt1062/teensy41/shell_children.h"
#include "platform/imxrt1062/teensy41/shell_retained.h"
static solar_os_tui_t tui;
static esp_err_t start(solar_os_context_t *){++starts;active_tui=&tui;return fail_start?ESP_FAIL:ESP_OK;}
static void stop(solar_os_context_t *){++stops;active_tui=nullptr;}
static void suspend(solar_os_context_t *){++suspends;}
static void resume(solar_os_context_t *ctx){++resumes;assert(ctx->argc==1 && !strcmp(ctx->argv[0],"saved argument"));assert(active_tui==&tui);}
static void launch(const solar_os_app_t *app,solar_os_launch_policy_t policy=SOLAR_OS_LAUNCH_REPLACE){char *args[]={const_cast<char *>("saved argument")};assert(solar_os_context_request_launch_ex(current_context(),app,1,args,policy)==ESP_OK);service_requests();}
static void request(uint32_t id,unsigned action){char b[16];snprintf(b,sizeof(b),"%lu",(unsigned long)id);char *args[]={const_cast<char *>("test"),b};request_app(&shell_context,2,args,action);}
static unsigned background_ticks;
static solar_os_context_t *background_context;
static bool background_event(solar_os_context_t *ctx,const solar_os_event_t *ev) {
 assert(ctx==background_context && ev->type==SOLAR_OS_EVENT_TICK);
 assert(foreground && !strcmp(foreground->name,"calc"));
 ++background_ticks;return true;
}
int main(){
 consoles[0].name="usb-shell";consoles[1].name="lcd-shell";consoles[1].local=true;
 for(auto &c:consoles){solar_os_context_init(&c.context,nullptr,nullptr);solar_os_context_set_shell_io(&c.context,&io);}
 solar_os_app_t apps[7]{};
 const char *names[]={"editor","files","calc","clock","notes","synth","nonresumable"};
 for(unsigned i=0;i<7;++i){apps[i].name=names[i];apps[i].app_class=SOLAR_OS_APP_CLASS_TUI;apps[i].start=start;apps[i].stop=stop;apps[i].suspend=suspend;apps[i].resume=resume;apps[i].flags=SOLAR_OS_APP_FLAG_RESUMABLE;}
 apps[6].flags=0;
 // A retained audio-style app receives opt-in timers without replacing editor
 // context/TUI; normal retained apps receive none, including timer wraparound.
 apps[0].flags|=SOLAR_OS_APP_FLAG_BACKGROUND_TICKS;
 apps[0].event=background_event;apps[0].tick_interval_ms=100;
 launch(&apps[0]);assert(suspend_app());background_context=&active().retained[0]->context;
 launch(&apps[1]);assert(suspend_app());launch(&apps[2]);auto *shown=app_frame;
 service_background_apps(100);assert(background_ticks==1 && app_frame==shown);
 service_background_apps(150);assert(background_ticks==1);
 service_background_apps(200);assert(background_ticks==2);
 active().retained[1]->background_tick=UINT32_MAX-20;
 service_background_apps(79);assert(background_ticks==3 && app_frame==shown);
 close_all_apps();assert(allocations==frees);
 apps[0].flags=SOLAR_OS_APP_FLAG_RESUMABLE;apps[0].event=nullptr;

 launch(&apps[0]);const auto id=app_frame->id;auto *saved=app_frame;
 assert(suspend_app() && !foreground && active().retained[0]==saved);
 // A retained singleton remains claimed on every console.
 current=1;launch(&apps[0]);assert(!foreground);current=0;
 request(id,1);assert(!foreground);service_session_request();assert(app_frame==saved && !active().retained[0]);
 // Context request follows the same suspension path.
 solar_os_context_request_suspend(current_context());service_requests();assert(!foreground);
 for(unsigned i=1;i<4;++i){launch(&apps[i]);assert(suspend_app());}
 launch(&apps[4]);assert(!suspend_app() && foreground==&apps[4]);
 const auto active_id=app_frame->id;
 // Resume is rejected while the owner has an active app.
 current=1;request(id,1);assert(!consoles[0].request_action);
 // Closing a retained chain while another app runs preserves the active app.
 request(id,2);assert(consoles[0].retained[3]==saved);current=0;
 service_session_request();assert(app_frame->id==active_id && foreground==&apps[4]);
 assert(!active().retained[3]);close_all_apps();assert(allocations==frees);
 // Nested Files/editor state is retained as a single session and stopped once.
 launch(&apps[1]);const auto chain_id=app_frame->id;launch(&apps[0],SOLAR_OS_LAUNCH_CHILD_RETURN);
 assert(app_frame->id==chain_id && app_frame->parent);assert(suspend_app());
 request(chain_id,1);service_session_request();solar_os_context_finish(current_context(),0,nullptr);service_requests();
 assert(foreground==&apps[1] && app_frame->id==chain_id);assert(suspend_app());
 request(chain_id,2);service_session_request();assert(!foreground && allocations==frees);
 // Non-resumable apps and failed starts/allocations never enter retained slots.
 launch(&apps[6]);assert(!suspend_app());close_all_apps();
 fail_alloc=true;launch(&apps[0]);fail_alloc=false;assert(!foreground);
 fail_start=true;launch(&apps[0]);fail_start=false;assert(!foreground && allocations==frees);
 // Cross-console requests execute only in the owner task, then IDs go stale.
 launch(&apps[0]);auto remote_id=app_frame->id;assert(suspend_app());current=1;
 shell_idle=false;request(remote_id,1);assert(!consoles[0].request_action);shell_idle=true;
 request(remote_id,1);assert(consoles[0].request_action==1 && !consoles[1].request_action);
 request(remote_id,2);assert(consoles[0].request_action==1);current=0;service_session_request();
 assert(foreground==&apps[0]);close_all_apps();current=1;request(remote_id,1);assert(!consoles[0].request_action);
 current=0;launch(&apps[5]);assert(suspend_app() && console_has_audio(active()));close_all_apps();assert(!console_has_audio(active()));
 uint32_t value;assert(!parse_app_id("-1",value) && !parse_app_id("4294967296",value) && !parse_app_id("1",value));
 assert(parse_app_id("4294967295",value));
 char *list[]={const_cast<char *>("session")};sk_shell_cmd_session(&shell_context,1,list);assert(output.find("usb-shell")!=std::string::npos);
 for(unsigned n=0;n<1000;++n){launch(&apps[0]);assert(suspend_app());char *fg[]={const_cast<char *>("fg")};sk_shell_cmd_fg(&shell_context,1,fg);service_session_request();assert(foreground==&apps[0]);close_all_apps();}
 assert(allocations==frees && starts==stops && boundaries>0 && suspends>0 && resumes>0);
 for(auto *p:claims)assert(!p);
 puts("PASS: retained state/arguments, nested chains, capacity, singleton/audio ownership, owner-task requests, stale IDs, allocation/start failure, disconnect cleanup and 1000 lifecycle cycles");
}
