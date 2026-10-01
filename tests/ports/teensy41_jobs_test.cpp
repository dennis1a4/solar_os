#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <unistd.h>
extern "C" {
#include "solar_os.h"
#include "solar_os_jobs.h"
#include "solar_os_task.h"
#include "jobs/solar_os_job_registry.h"
#include "solar_os_shell.h"
#include "solar_os_shell_io.h"
#include "solar_os_shell_parse.h"
#include "solar_os_schedule.h"
#include "solar_os_memory.h"
#include "solar_os_time.h"
}
#define EXTMEM
#define DMAMEM
#define configASSERT assert
#define portMAX_DELAY 0xffffffffU
struct Console {solar_os_context_t context;solar_os_shell_session_t *shell;solar_os_port_handle_t port;const char *name;};
struct solar_os_shell_session {solar_os_shell_io_t io;};
static TaskHandle_t background_task=reinterpret_cast<void *>(1);
static Console *background_console;
static int console_gate;
static uint64_t clock_ms;
static unsigned allocations,frees;
static bool fail_alloc;
static solar_os_port_driver_t drivers[4];static unsigned driver_count;
static solar_os_schedule_script_runner_t scheduled_runner;
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(){return background_task;}
extern "C" void vTaskDelay(uint32_t ms){clock_ms+=ms;}
static void xSemaphoreTakeRecursive(int,unsigned){}
static void console_yield(){}
extern "C" uint64_t solar_os_time_uptime_ms(){return clock_ms;}
extern "C" int64_t esp_timer_get_time(){return clock_ms*1000;}
extern "C" const char *esp_err_to_name(esp_err_t e){return e==ESP_OK?"OK":"error";}
extern "C" size_t strlcpy(char *out,const char *s,size_t n) noexcept {size_t l=strlen(s);if(n){size_t z=l<n-1?l:n-1;memcpy(out,s,z);out[z]=0;}return l;}
extern "C" void *solar_os_memory_calloc(size_t n,size_t size,solar_os_memory_class_t,const char *){if(fail_alloc)return nullptr;void *p=calloc(n,size);if(p)++allocations;return p;}
extern "C" void solar_os_memory_free(void *p){if(p){++frees;free(p);}}
extern "C" esp_err_t solar_os_log_write(solar_os_log_level_t,const char *,const char *,...){return ESP_OK;}
extern "C" bool solar_os_task_can_create(uint32_t,solar_os_task_role_t,bool){return true;}
extern "C" void solar_os_task_note_wait_queued(){}
extern "C" void solar_os_task_note_wait_finished(bool){}
extern "C" esp_err_t solar_os_schedule_init(){return ESP_OK;}
extern "C" void solar_os_schedule_set_script_runner(solar_os_schedule_script_runner_t f){scheduled_runner=f;}
extern "C" esp_err_t solar_os_port_register(const solar_os_port_driver_t *d){assert(driver_count<4);drivers[driver_count++]=*d;return ESP_OK;}
extern "C" esp_err_t solar_os_port_claim(const char *name,const char *,solar_os_port_handle_t *h){memset(h,0,sizeof(*h));for(unsigned i=0;i<4;++i)if(!strcmp(name,drivers[i].name)){h->index=i;return ESP_OK;}return ESP_FAIL;}
extern "C" solar_os_shell_session_t *solar_os_shell_session_create(){return static_cast<solar_os_shell_session_t *>(solar_os_memory_calloc(1,sizeof(solar_os_shell_session_t),SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"test"));}
extern "C" void solar_os_shell_session_destroy(solar_os_shell_session_t *s){solar_os_memory_free(s);}
extern "C" solar_os_shell_io_t *solar_os_shell_session_io(solar_os_shell_session_t *s){return &s->io;}
extern "C" void solar_os_shell_io_init_port(solar_os_shell_io_t *io,const solar_os_port_handle_t *h,uint16_t,uint16_t){io->port=*h;}
extern "C" void solar_os_shell_io_set_terminal_profile(solar_os_shell_io_t *,solar_os_shell_terminal_profile_t){}
extern "C" void solar_os_shell_io_capture_output(solar_os_shell_io_t *,solar_os_context_t *){}
extern "C" esp_err_t solar_os_shell_session_start(solar_os_context_t *ctx,solar_os_shell_session_t *s,solar_os_shell_io_t *,bool,bool){solar_os_context_set_shell_session(ctx,s);solar_os_context_set_shell_io(ctx,&s->io);return ESP_OK;}
extern "C" esp_err_t solar_os_shell_io_write(solar_os_shell_io_t *io,const char *s){size_t n=0;auto &d=drivers[io->port.index];return d.write(d.user,reinterpret_cast<const uint8_t *>(s),strlen(s),&n);}
extern "C" esp_err_t solar_os_shell_io_writeln(solar_os_shell_io_t *io,const char *s){solar_os_shell_io_write(io,s);return solar_os_shell_io_write(io,"\n");}
extern "C" esp_err_t solar_os_shell_io_printf(solar_os_shell_io_t *io,const char *fmt,...){char text[512];va_list ap;va_start(ap,fmt);vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);return solar_os_shell_io_write(io,text);}
extern "C" esp_err_t solar_os_shell_resolve_path(solar_os_context_t *,const char *p,char *out,size_t n){if(strlen(p)>=n)return ESP_ERR_INVALID_SIZE;strlcpy(out,p,n);return ESP_OK;}
extern "C" bool sk_background_wait(uint32_t);
extern "C" esp_err_t solar_os_shell_execute_command(solar_os_context_t *ctx,const char *line){
 if(!strncmp(line,"wait ",5))assert(sk_background_wait(strtoul(line+5,nullptr,10)*1000));
 else if(!strncmp(line,"echo ",5))solar_os_shell_io_writeln(solar_os_context_shell_io(ctx),line+5);
 return ESP_OK;
}
static void process_list(solar_os_shell_io_t *){}
static bool process_command(solar_os_context_t *,int,char **){return false;}
#include "platform/imxrt1062/teensy41/background_jobs.h"
static void tick(){clock_ms+=25;solar_os_jobs_tick(nullptr,uint32_t(clock_ms));}
static void file(const char *path,const std::string &s){FILE *f=fopen(path,"w");assert(f);assert(fwrite(s.data(),1,s.size(),f)==s.size());assert(!fclose(f));}
int main(){
 (void)background_run;(void)background_stack;(void)background_tcb;
 background_begin();assert(solar_os_jobs_count()==4 && scheduled_runner);
 char path[]="/tmp/solaros-jobs-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
 file(path,"echo BEFORE\nwait 2\necho AFTER\n");
 assert(scheduled_runner(path)==ESP_OK);assert(script_jobs[0].pending && !script_jobs[0].file);
 tick();tick();tick();assert(script_jobs[0].wake_ms>clock_ms);
 for(unsigned i=0;i<3;++i)assert(enqueue_script(path)==ESP_OK);
 assert(enqueue_script(path)==ESP_ERR_NO_MEM);
 for(unsigned i=0;i<10;++i){tick();}
 assert(!strstr(script_jobs[0].output,"AFTER"));
 assert(solar_os_jobs_stop(nullptr,"script0")==ESP_OK);assert(!script_jobs[0].file && !script_jobs[0].console.shell);
 assert(enqueue_script(path)==ESP_OK);clock_ms+=3000;
 for(unsigned i=0;i<100;++i)tick();
 for(auto &j:script_jobs)assert(strstr(j.output,"AFTER") && !j.file && !j.console.shell);
 assert(allocations==frees);
 file(path,"echo x | cat\necho MUST_NOT_RUN\n");assert(enqueue_script(path)==ESP_OK);tick();tick();
 solar_os_job_status_t state;assert(solar_os_jobs_get(0,&state));assert(state.last_error==ESP_ERR_NOT_SUPPORTED);
 assert(!strstr(script_jobs[0].output,"MUST_NOT_RUN"));
 file(path,std::string(220,'x')+"\n");assert(enqueue_script(path)==ESP_OK);tick();tick();assert(!script_jobs[0].file);
 file(path,"calc\necho MUST_NOT_RUN\n");assert(enqueue_script(path)==ESP_OK);tick();tick();assert(!script_jobs[0].file);
 file(path,"echo valid\n");fail_alloc=true;assert(enqueue_script(path)==ESP_OK);tick();fail_alloc=false;
 assert(solar_os_jobs_get(0,&state) && state.last_error==ESP_ERR_NO_MEM);assert(!script_jobs[0].file);
 assert(enqueue_script("/tmp/missing-solaros-job-file")==ESP_OK);tick();assert(solar_os_jobs_get(0,&state) && state.last_error==ESP_ERR_NOT_FOUND);
 std::string big;for(unsigned i=0;i<80;++i)big+="echo "+std::string(150,'a')+"\n";big+="echo END\n";
 file(path,big);assert(enqueue_script(path)==ESP_OK);for(unsigned i=0;i<90;++i)tick();
 assert(script_jobs[0].dropped>0 && script_jobs[0].output_size==4095 && strstr(script_jobs[0].output,"END"));
 file(path,"echo cycle\n");for(unsigned i=0;i<1000;++i){assert(enqueue_script(path)==ESP_OK);tick();tick();tick();}
 assert(allocations==frees);assert(unlink(path)==0);
 puts("PASS: shared job lifecycle, four-slot admission, deferred file open, cooperative wait/stop, output bounds, invalid commands/lines, missing files, allocation failure and 1000 cleanup cycles");
}
