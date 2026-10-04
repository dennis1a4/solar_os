// Included by shell_dual.cpp: shared job lifecycle plus bounded cooperative
// script slots. The owner task executes one script line per job tick.
#include <cstdio>
#include <cerrno>
struct BackgroundJob {
    Console console;
    FILE *file;
    bool pending;
    uint64_t wake_ms;
    uint32_t line,dropped;
    char path[SOLAR_OS_SCHEDULE_VALUE_MAX];
    char output[4096];
    size_t output_size;
};
EXTMEM static BackgroundJob script_jobs[4];
// Non-realtime, cooperative script worker. Keep TCB and interrupt stacks internal.
// Static EXTMEM is carved out before the external heap is initialized.
EXTMEM static StackType_t background_stack[4096] __attribute__((aligned(8)));
static StaticTask_t background_tcb;
static const char *script_names[]={"script0","script1","script2","script3"};
static BackgroundJob *executing_job;
static esp_err_t job_write(void *user,const uint8_t *data,size_t length,size_t *written) {
    auto &j=*static_cast<BackgroundJob *>(user);
    constexpr size_t capacity=sizeof(j.output)-1;
    if(length>=capacity) {
        j.dropped+=j.output_size+length-capacity;
        memcpy(j.output,data+length-capacity,capacity);j.output_size=capacity;
    } else {
        const size_t discard=j.output_size+length>capacity?j.output_size+length-capacity:0;
        if(discard){memmove(j.output,j.output+discard,j.output_size-discard);j.output_size-=discard;j.dropped+=discard;}
        memcpy(j.output+j.output_size,data,length);j.output_size+=length;
    }
    j.output[j.output_size]=0;*written=length;return ESP_OK;
}
static void job_cleanup(unsigned slot) {
    auto &j=script_jobs[slot];
    if(j.file){fclose(j.file);j.file=nullptr;}
    if(j.console.shell){solar_os_shell_session_destroy(j.console.shell);j.console.shell=nullptr;}
    memset(&j.console.context,0,sizeof(j.console.context));j.wake_ms=0;j.pending=false;
}
static void job_finish(unsigned slot,esp_err_t error) {
    uint32_t generation=0;
    solar_os_jobs_get_generation(script_names[slot],&generation);
    job_cleanup(slot);
    solar_os_jobs_mark_stopped(script_names[slot],generation,error);
}
static esp_err_t script_start(unsigned slot,int argc,char **argv) {
    if(argc!=1 || !argv || argv[0][0]!='/' || strlen(argv[0])>=sizeof(script_jobs[slot].path))return ESP_ERR_INVALID_ARG;
    auto &j=script_jobs[slot];job_cleanup(slot);
    j.line=j.dropped=0;j.output_size=0;j.output[0]=0;
    strlcpy(j.path,argv[0],sizeof(j.path));
    j.pending=true;
    return ESP_OK;
}
static esp_err_t script_open(unsigned slot) {
    auto &j=script_jobs[slot];j.pending=false;
    j.file=fopen(j.path,"r");if(!j.file)return ESP_ERR_NOT_FOUND;
    j.console.shell=solar_os_shell_session_create();
    if(!j.console.shell)return ESP_ERR_NO_MEM;
    solar_os_context_init(&j.console.context,nullptr,nullptr);
    auto *io=solar_os_shell_session_io(j.console.shell);
    solar_os_shell_io_init_port(io,&j.console.port,80,24);
    solar_os_shell_io_set_terminal_profile(io,SOLAR_OS_SHELL_TERMINAL_PROFILE_DUMB);
    solar_os_shell_session_start(&j.console.context,j.console.shell,io,false,false);
    j.output_size=0;j.output[0]=0;
    solar_os_jobs_note_resource(script_names[slot],SOLAR_OS_JOB_RESOURCE_FILE,j.path,"script input");
    return ESP_OK;
}

extern "C" bool sk_background_wait(uint32_t ms) {
    if(!background_task || xTaskGetCurrentTaskHandle()!=background_task || !executing_job)return false;
    executing_job->wake_ms=solar_os_time_uptime_ms()+ms;return true;
}
static bool script_command_allowed(const char *command) {
    // Interactive apps, scripts within scripts, watch and lifecycle commands
    // need different execution contracts; never launch them from this runner.
    static const char *names[]={"echo","wait","pwd","cd","ls","cat","mkdir","cp","mv","rm",
        "version","board","status","mem","uptime","top","port","date","time"};
    for(auto *name:names)if(!strcmp(command,name))return true;
    return false;
}
static bool script_tick(unsigned slot) {
    auto &j=script_jobs[slot];
    background_console=&j.console;
    if(j.pending) {
        const auto error=script_open(slot);
        if(error!=ESP_OK) {
            snprintf(j.output,sizeof(j.output),"job: cannot open script: %s\n",esp_err_to_name(error));
            j.output_size=strlen(j.output);job_finish(slot,error);
        }
        return true;
    }
    if(!j.file || !j.console.shell)return false;
    if(solar_os_time_uptime_ms()<j.wake_ms)return true;
    background_console=&j.console;executing_job=&j;
    char line[193];
    if(!fgets(line,sizeof(line),j.file)) {
        const auto result=ferror(j.file)?ESP_FAIL:ESP_OK;
        executing_job=nullptr;job_finish(slot,result);return true;
    }
    ++j.line;
    char *start=line;while(*start==' ' || *start=='\t')++start;
    bool failed=false;
    if(!strchr(line,'\n') && !feof(j.file))failed=true;
    line[strcspn(line,"\r\n")]=0;
    if(*start && *start!='#' && !failed) {
        char parsed[sizeof(line)];strlcpy(parsed,start,sizeof(parsed));char *argv[16];
        const auto result=solar_os_shell_tokenize(parsed,argv,16);
        if(result.error!=SOLAR_OS_SHELL_PARSE_OK || !result.argc || !script_command_allowed(argv[0]))failed=true;
        else solar_os_shell_execute_command(&j.console.context,start);
    }
    if(failed)solar_os_shell_io_printf(solar_os_shell_session_io(j.console.shell),
        "job: line %lu rejected (syntax, length or unsupported background command)\n",(unsigned long)j.line);
    executing_job=nullptr;
    if(failed)job_finish(slot,ESP_ERR_NOT_SUPPORTED);
    return true;
}
#define SCRIPT_CALLBACKS(N) \
static esp_err_t script_start_##N(solar_os_context_t *,int argc,char **argv){return script_start(N,argc,argv);} \
static void script_stop_##N(solar_os_context_t *){job_cleanup(N);} \
static bool script_event_##N(solar_os_context_t *,const solar_os_event_t *){return script_tick(N);}
SCRIPT_CALLBACKS(0) SCRIPT_CALLBACKS(1) SCRIPT_CALLBACKS(2) SCRIPT_CALLBACKS(3)
#undef SCRIPT_CALLBACKS
#define SCRIPT_JOB(N) {script_names[N],"cooperative shell script",SOLAR_OS_JOB_KIND_BACKGROUND,script_start_##N,script_stop_##N,script_event_##N,0,false,25,25,nullptr,nullptr}
static const solar_os_job_t script_descriptors[]={SCRIPT_JOB(0),SCRIPT_JOB(1),SCRIPT_JOB(2),SCRIPT_JOB(3)};
#undef SCRIPT_JOB
static const solar_os_job_registry_entry_t script_registry[]={
    {"script0","background script slot 0",&script_descriptors[0]},
    {"script1","background script slot 1",&script_descriptors[1]},
    {"script2","background script slot 2",&script_descriptors[2]},
    {"script3","background script slot 3",&script_descriptors[3]}};
extern "C" size_t solar_os_job_registry_count(){return 4;}
extern "C" const solar_os_job_registry_entry_t *solar_os_job_registry_get(size_t i){return i<4?&script_registry[i]:nullptr;}
extern "C" const solar_os_job_registry_entry_t *solar_os_job_registry_find(const char *name){for(auto &e:script_registry)if(name && !strcmp(name,e.name))return &e;return nullptr;}
static esp_err_t enqueue_script(const char *path) {
    for(unsigned i=0;i<4;++i) {
        solar_os_job_status_t state;
        if(solar_os_jobs_get(i,&state) && state.state!=SOLAR_OS_JOB_RUNNING && state.state!=SOLAR_OS_JOB_WAITING) {
            char *argv[]={const_cast<char *>(path)};
            return solar_os_jobs_start(nullptr,script_names[i],1,argv);
        }
    }
    return ESP_ERR_NO_MEM;
}
static void print_job(solar_os_shell_io_t *io,unsigned i) {
    solar_os_job_status_t state;if(!solar_os_jobs_get(i,&state))return;
    auto &j=script_jobs[i];
    const char *label=state.last_error!=ESP_OK?"failed":state.state==SOLAR_OS_JOB_RUNNING?
        (j.pending?"queued":j.wake_ms>solar_os_time_uptime_ms()?"waiting":"running"):solar_os_job_state_name(state.state);
    solar_os_shell_io_printf(io,"%s %-8s line=%lu dropped=%lu result=%s %s\n",script_names[i],label,
        (unsigned long)j.line,(unsigned long)j.dropped,esp_err_to_name(state.last_error),j.path);
}
extern "C" void solar_os_shell_cmd_jobs(solar_os_context_t *ctx,int argc,char **) {
    auto *io=solar_os_context_shell_io(ctx);
    if(argc!=1){solar_os_shell_io_writeln(io,"usage: jobs");return;}
    process_list(io);
    for(unsigned i=0;i<4;++i)print_job(io,i);
}
extern "C" void solar_os_shell_cmd_job(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if(process_command(ctx,argc,argv))return;
    if(argc==2 && !strcmp(argv[1],"status")){solar_os_shell_cmd_jobs(ctx,1,argv);return;}
    if(argc<3){solar_os_shell_io_writeln(io,"usage: job start script[0-3] /path.sh | job status|stop|output scriptN");return;}
    int slot=-1;for(unsigned i=0;i<4;++i)if(!strcmp(argv[2],script_names[i]))slot=int(i);
    if(argc==4 && !strcmp(argv[1],"start")) {
        char path[SOLAR_OS_SCHEDULE_VALUE_MAX];
        esp_err_t err=solar_os_shell_resolve_path(ctx,argv[3],path,sizeof(path));
        if(err==ESP_OK) {
            if(!strcmp(argv[2],"script"))err=enqueue_script(path);
            else if(slot>=0){char *args[]={path};err=solar_os_jobs_start(ctx,script_names[slot],1,args);}
            else err=ESP_ERR_NOT_FOUND;
        }
        solar_os_shell_io_printf(io,"job start: %s\n",esp_err_to_name(err));return;
    }
    if(argc!=3 || slot<0){solar_os_shell_io_writeln(io,"job: expected script0, script1, script2 or script3");return;}
    if(!strcmp(argv[1],"status"))print_job(io,unsigned(slot));
    else if(!strcmp(argv[1],"output"))solar_os_shell_io_writeln(io,script_jobs[slot].output);
    else if(!strcmp(argv[1],"stop"))solar_os_shell_io_printf(io,"job stop: %s\n",esp_err_to_name(solar_os_jobs_stop(ctx,script_names[slot])));
    else solar_os_shell_io_writeln(io,"job: unknown subcommand");
}
static void background_begin() {
    memset(script_jobs,0,sizeof(script_jobs));
    for(unsigned i=0;i<4;++i) {
        auto &j=script_jobs[i];j.console.name=script_names[i];
        solar_os_port_driver_t driver={script_names[i],"background script output",SOLAR_OS_PORT_CAP_WRITE,
            nullptr,job_write,nullptr,nullptr,&j};
        configASSERT(solar_os_port_register(&driver)==ESP_OK);
        configASSERT(solar_os_port_claim(script_names[i],script_names[i],&j.console.port)==ESP_OK);
    }
    configASSERT(solar_os_jobs_init()==ESP_OK);
    configASSERT(solar_os_schedule_init()==ESP_OK);
    solar_os_schedule_set_script_runner(enqueue_script);
}
static void background_run(void *) {
    xSemaphoreTakeRecursive(console_gate,portMAX_DELAY);
    while(true) {
        if(sk_power_cleaning()) {
            for(unsigned i=0;i<4;++i)if(script_jobs[i].file || script_jobs[i].pending || script_jobs[i].console.shell)job_finish(i,ESP_OK);
        } else if(!sk_power_requested())solar_os_jobs_tick(nullptr,uint32_t(solar_os_time_uptime_ms()));
        background_console=nullptr;console_yield();
    }
}
