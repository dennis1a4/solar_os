// Single-VM process adapter. Every field is protected by console_gate. The
// worker releases that gate only at VM/I/O polling points, never inside a
// filesystem transaction. Its stack remains allocated while paused.
static solar_os_port_handle_t process_port;
static void process_append(const uint8_t *data,size_t length) {
    constexpr size_t cap=sizeof(process.output)-1;
    if(length>=cap) {process.dropped+=process.output_size+length-cap;memcpy(process.output,data+length-cap,cap);process.output_size=cap;}
    else {size_t n=process.output_size+length>cap?process.output_size+length-cap:0;
        if(n){memmove(process.output,process.output+n,process.output_size-n);process.output_size-=n;process.dropped+=n;}
        memcpy(process.output+process.output_size,data,length);process.output_size+=length;}
    process.output[process.output_size]=0;
}
static esp_err_t process_write(void *,const uint8_t *data,size_t length,size_t *written) {
    process_append(data,length);*written=length;
    if(process.owner_console && !process.paused && !process.detached) {
        auto *io=solar_os_shell_session_io(process.owner_console->shell);
        return solar_os_shell_io_write_raw(io,reinterpret_cast<const char *>(data),length);
    }
    return ESP_OK;
}
static int process_pop() {
    if(process.input_head==process.input_tail)return -1;
    int ch=process.input[process.input_tail];process.input_tail=(process.input_tail+1)%sizeof(process.input);return ch;
}
extern "C" bool sk_process_poll_cancel() {
    do {console_yield();} while(process.paused && !process.stopping);
#if SK_GRAPHICS
    if(!process.detached && solar_os_context_graphics_active(&process.frame->context)) {
        extern void sk_python_gfx_key(int);
        for(int ch;(ch=process_pop())>=0;)sk_python_gfx_key(ch);
    }
#endif
    const bool interrupted=process.interrupt;process.interrupt=false;
    return process.stopping || interrupted;
}
extern "C" int sk_process_stdin() {
    process.input_wait=true;
    while(true) {
        if(sk_process_poll_cancel()) {process.input_wait=false;return -2;}
        // A detached process must never consume keys from the shell.
        if(!process.detached) {int ch=process_pop();if(ch>=0){process.input_wait=false;return ch;}}
    }
}
extern "C" bool sk_process_graphics_allowed() {return !process.detached && !process.paused && process.owner_console && process.owner_console->local;}
static void process_run(void *) {
    xSemaphoreTakeRecursive(console_gate,portMAX_DELAY);
    auto *ctx=&process.frame->context;
    process.executing=true;
    const auto result=process.start(ctx);
    process.executing=false;
    if(result!=ESP_OK)solar_os_context_finish(ctx,1,"python: worker start failed");
    while(!ctx->exit_requested && !process.stopping) {
        if(!process.paused && !process.detached) {
            const int ch=process_pop();
            if(ch>=0){solar_os_event_t e{};e.type=SOLAR_OS_EVENT_CHAR;e.data.ch=ch;process.executing=true;process.event(ctx,&e);process.executing=false;}
        }
        console_yield();
    }
    // Cleanup always executes on the VM's own stack, including GC finalizers.
    process.stop(ctx);
    if(!ctx->exit_requested)solar_os_context_finish(ctx,process.stopping?130:0,nullptr);
    process.done=true;
    xSemaphoreGiveRecursive(console_gate);
    solar_os_task_delete_internal(nullptr);
}
extern "C" esp_err_t sk_process_start(solar_os_context_t *ctx,
    esp_err_t (*start)(solar_os_context_t *),bool (*event)(solar_os_context_t *,const solar_os_event_t *),void (*stop)(solar_os_context_t *)) {
    if(process.frame || !app_frame || app_frame->parent)return ESP_ERR_INVALID_STATE;
    if(!solar_os_task_admit("python-job",40960,SOLAR_OS_TASK_ROLE_BACKGROUND,false))return ESP_ERR_NO_MEM;
    char cwd[SOLAR_OS_STORAGE_PATH_MAX];
    if(solar_os_shell_resolve_path(ctx,nullptr,cwd,sizeof(cwd))!=ESP_OK)return ESP_FAIL;
    memset(&process,0,sizeof(process));
    process.console.shell=solar_os_shell_session_create();if(!process.console.shell)return ESP_ERR_NO_MEM;
    process.console.port=process_port;process.console.name="python-job";process.console.local=active().local;
    process.console.frame=app_frame;process.console.app=app_frame->app;
    auto *io=solar_os_shell_session_io(process.console.shell);
    solar_os_shell_io_init_port(io,&process_port,80,24);
    solar_os_shell_io_set_terminal_profile(io,SOLAR_OS_SHELL_TERMINAL_PROFILE_DUMB);
    // Start the private shell context without running startup scripts.
    solar_os_context_init(&process.console.context,nullptr,solar_os_context_gfx(ctx));
    solar_os_shell_session_start(&process.console.context,process.console.shell,io,false,false);
    solar_os_shell_set_cwd(&process.console.context,cwd);
    process.output_size=0;process.output[0]=0;
    process.owner_console=&active();process.frame=app_frame;
    solar_os_context_set_shell_session(ctx,process.console.shell);solar_os_context_set_shell_io(ctx,io);
    process.start=start;process.event=event;process.stop=stop;process.started_ms=solar_os_time_uptime_ms();
    if(solar_os_task_create_pinned_internal(process_run,"python-job",40960,nullptr,1,&process.task,tskNO_AFFINITY,SOLAR_OS_TASK_ROLE_BACKGROUND)!=pdPASS) {
        solar_os_context_set_shell_session(ctx,session);solar_os_context_set_shell_io(ctx,solar_os_shell_session_io(session));
        solar_os_shell_session_destroy(process.console.shell);memset(&process,0,sizeof(process));return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
extern "C" void sk_process_stop(solar_os_context_t *ctx) {
    if(!process.frame || &process.frame->context!=ctx)return;
    // Normal lifecycle only reaps a completed worker. Explicit close/kill and
    // disconnect are intercepted before the generic frame destructor.
    configASSERT(process.done);
    while(eTaskGetState(process.task)!=eSuspended)console_yield();
    configASSERT(solar_os_task_wait_done(process.task,&process.done,1000));
    solar_os_shell_session_destroy(process.console.shell);
    memset(&process,0,sizeof(process));
}
extern "C" bool sk_process_event(solar_os_context_t *,const solar_os_event_t *event) {
    if(event->type!=SOLAR_OS_EVENT_CHAR || process.done)return false;
    const uint8_t ch=event->data.ch;
    if(ch==SOLAR_OS_KEY_APP_EXIT){process.stopping=true;process.paused=false;return true;}
    if(ch==3 && process.executing){process.interrupt=true;return true;}
    unsigned next=(process.input_head+1)%sizeof(process.input);
    if(next!=process.input_tail){process.input[process.input_head]=ch;process.input_head=next;}
    return true;
}
extern "C" void sk_process_suspend(solar_os_context_t *) {process.paused=true;}
extern "C" void sk_process_resume(solar_os_context_t *) {
    process.owner_console=&active();process.detached=false;process.paused=false;
    solar_os_shell_io_write_len(solar_os_shell_session_io(session),process.output,process.output_size);
}
static void process_release_detached() {
    configASSERT(process.detached && process.done);
    auto *frame=process.frame;
    sk_process_stop(&frame->context);
    solar_os_app_registry_release(frame->app,"python-job");solar_os_memory_free(frame);
}
static bool process_close_request(uint32_t id) {
    if(!process.frame || process.frame->id!=id)return false;
    if(process.done) {
        if(process.detached){process_release_detached();return true;}
        return false;
    }
    process.stopping=true;process.paused=false;return true;
}
static bool process_disconnect_frame(AppFrame *frame) {
    if(process.frame!=frame)return false;
    // An explicitly backgrounded process has already left this console's list.
    // Foreground/suspended processes are cancelled, but retain resources until
    // their worker acknowledges cancellation and completes cleanup.
    process.stopping=true;process.paused=false;process.detached=true;
    solar_os_app_registry_release(frame->app,owner);
    configASSERT(solar_os_app_registry_claim(frame->app,"python-job",nullptr,0)==ESP_OK);
    process.owner_console=nullptr;process.console.local=false;
    solar_os_context_set_gfx(&frame->context,nullptr);
    return true;
}
static bool process_foreground_request(uint32_t id) {
    if(!process.frame || process.frame->id!=id || !process.detached)return false;
    auto *io=solar_os_shell_session_io(session);
    if(app_frame){solar_os_shell_io_writeln(io,"fg: suspend the current app first");return true;}
    // Called by a shell command; it becomes idle when command dispatch returns.
    active().request_id=id;active().request_action=3;return true;
}
static void process_attach() {
    if(!process.frame || !process.detached || app_frame)return;
    solar_os_app_registry_release(process.frame->app,"python-job");
    configASSERT(solar_os_app_registry_claim(process.frame->app,owner,nullptr,0)==ESP_OK);
    app_frame=process.frame;process.owner_console=&active();process.console.local=active().local;
    extern solar_os_gfx_t *sk_lcd_gfx();
    solar_os_context_set_gfx(&app_frame->context,active().local?sk_lcd_gfx():nullptr);
    select_frame();sk_process_resume(&app_frame->context);
    if(process.done)finish_app();
}
extern "C" void sk_shell_cmd_bg(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);uint32_t id=0;
    if(argc==1 && active().retained[0])id=active().retained[0]->id;
    else if(argc!=2 || !parse_app_id(argv[1],id)){solar_os_shell_io_writeln(io,"usage: bg [suspended Python session ID]");return;}
    if(!process.frame || process.frame->id!=id || process.detached || process.owner_console!=&active()) {
        solar_os_shell_io_writeln(io,"bg: only this console's suspended Python process can run detached");return;
    }
    int slot=-1;for(unsigned i=0;i<4;++i)if(active().retained[i]==process.frame)slot=i;
    if(slot<0 || process.done){solar_os_shell_io_writeln(io,"bg: suspend a running Python session first");return;}
    take_retained(slot);process.detached=true;process.paused=false;process.owner_console=nullptr;
    process.console.local=false;solar_os_context_set_gfx(&process.frame->context,nullptr);
    solar_os_app_registry_release(process.frame->app,owner);
    configASSERT(solar_os_app_registry_claim(process.frame->app,"python-job",nullptr,0)==ESP_OK);
    solar_os_shell_io_printf(io,"[%lu] Python running in background\n",(unsigned long)id);
}
static void process_list(solar_os_shell_io_t *io) {
    if(!process.frame)return;
    const char *state=process.done?"done":process.stopping?"stopping":process.paused?"suspended":process.input_wait?"waiting-input":process.detached?"running":"foreground";
    uint64_t seconds=(solar_os_time_uptime_ms()-process.started_ms)/1000;
    solar_os_shell_io_printf(io,"%lu python %-13s %02lu:%02lu:%02lu dropped=%lu\n",(unsigned long)process.frame->id,state,
        (unsigned long)(seconds/3600),(unsigned long)(seconds/60%60),(unsigned long)(seconds%60),(unsigned long)process.dropped);
}
static bool process_command(solar_os_context_t *ctx,int argc,char **argv) {
    uint32_t id;
    if(argc!=3 || !parse_app_id(argv[2],id))return false;
    auto *io=solar_os_context_shell_io(ctx);
    if(!process.frame || process.frame->id!=id){solar_os_shell_io_writeln(io,"job: no such process ID");return true;}
    if(!strcmp(argv[1],"output"))solar_os_shell_io_write_len(io,process.output,process.output_size);
    else if(!strcmp(argv[1],"status"))process_list(io);
    else if(!strcmp(argv[1],"stop") || !strcmp(argv[1],"kill")) {
        if(!process_close_request(id)){active().request_id=id;active().request_action=2;}
        solar_os_shell_io_writeln(io,"python: stop requested (cooperative; resources retained until exit)");
    } else solar_os_shell_io_writeln(io,"usage: job status|output|stop|kill ID");
    return true;
}
static void process_begin() {
    memset(&process,0,sizeof(process));
    const solar_os_port_driver_t driver={"process0","detachable process output",SOLAR_OS_PORT_CAP_WRITE,nullptr,process_write,nullptr,nullptr,nullptr};
    configASSERT(solar_os_port_register(&driver)==ESP_OK);
    configASSERT(solar_os_port_claim("process0","python-job",&process_port)==ESP_OK);
}

static void process_reap_stopped() {
    if(!process.frame || !process.done || !process.stopping || eTaskGetState(process.task)!=eSuspended)return;
    if(process.detached){process_release_detached();return;}
    if(process.owner_console!=&active() || active().request_action)return;
    for(auto *frame:active().retained)if(frame==process.frame){active().request_id=frame->id;active().request_action=2;return;}
}
extern "C" void sk_shell_cmd_tail(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);unsigned lines=10;const char *name=nullptr;
    if(argc==2)name=argv[1];
    else if(argc==4 && !strcmp(argv[1],"-n")) {
        char *end;unsigned long n=strtoul(argv[2],&end,10);
        if(*argv[2] && !*end && n && n<=10000){lines=n;name=argv[3];}
    }
    if(!name){solar_os_shell_io_writeln(io,"usage: tail [-n 1..10000] FILE (snapshot; no -f yet)");return;}
    char path[SOLAR_OS_STORAGE_PATH_MAX];
    if(solar_os_shell_resolve_path(ctx,name,path,sizeof(path))!=ESP_OK){solar_os_shell_io_writeln(io,"tail: invalid path");return;}
    FILE *f=fopen(path,"r");if(!f){solar_os_shell_io_writeln(io,"tail: cannot open file");return;}
    char chunk[256];long start=0,end=0,pos=0;bool failed=false,cancelled=false,found=false;unsigned seen=0;
    if(fseek(f,0,SEEK_END) || (end=ftell(f))<0)failed=true;
    pos=end;
    while(!failed && !found && pos>0) {
        long begin=pos>long(sizeof(chunk))?pos-long(sizeof(chunk)):0;
        size_t count=pos-begin;
        if(fseek(f,begin,SEEK_SET) || fread(chunk,1,count,f)!=count){failed=true;break;}
        for(size_t i=count;i>0;--i)if(chunk[i-1]=='\n' && begin+long(i)<end && ++seen==lines){start=begin+i;found=true;break;}
        pos=begin;if(sk_console_poll_cancel(true)){cancelled=true;break;}
    }
    if(!failed && !cancelled && fseek(f,start,SEEK_SET))failed=true;
    for(pos=start;!failed && !cancelled && pos<end;) {
        size_t n=size_t(end-pos);if(n>sizeof(chunk))n=sizeof(chunk);
        if(fread(chunk,1,n,f)!=n){failed=true;break;}
        solar_os_shell_io_write_len(io,chunk,n);pos+=n;
        if(sk_console_poll_cancel(true))cancelled=true;
    }
    fclose(f);
    if(failed)solar_os_shell_io_writeln(io,"tail: read failed (file may have changed)");
    if(cancelled)solar_os_shell_io_writeln(io,"tail: cancelled");
}

// Native file operations have their own storage mutex. Release the console
// gate around them so a slow card flush cannot stall unrelated text/UI work.
// No VM state is touched until the gate is reacquired; stop never deletes a
// live worker, so its buffers and file object remain valid throughout the call.
extern "C" void sk_process_io_unlock() {xSemaphoreGiveRecursive(console_gate);}
extern "C" void sk_process_io_lock() {
    const int saved_errno=errno;
    xSemaphoreTakeRecursive(console_gate,portMAX_DELAY);
    while(process.paused && !process.stopping)console_yield();
    errno=saved_errno;
}
