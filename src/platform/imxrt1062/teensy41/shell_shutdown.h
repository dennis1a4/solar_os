// Included after the process and retained-session adapters. console_gate owns
// all app/job state; the coordinator uses a bounded lock wait (zero-wait polling can starve behind consoles).
static bool shutdown_chain_safe(AppFrame *frame) {
    for(;frame;frame=frame->parent) {
#if SK_BACKGROUND_JOBS
        if(frame==process.frame)continue;
#endif
        if(strcmp(frame->app->name,"com"))return false;
    }
    return true;
}
extern "C" int sk_shutdown_preflight() {
    if(xSemaphoreTakeRecursive(console_gate,pdMS_TO_TICKS(20))!=pdTRUE)return 0;
    Console *entries[3];unsigned count=console_entries(entries);int result=1;
    for(unsigned i=0;i<count;++i) {
        auto *c=entries[i];
        if(c->dispatching) {result=0;break;}
        if(!shutdown_chain_safe(c->frame)) {result=-1;break;}
        for(auto *f:c->retained)if(!shutdown_chain_safe(f)) {result=-1;break;}
        if(result<0)break;
    }
#if SK_BACKGROUND_JOBS
    // Resume the whole worker, including its native I/O return boundary.
    if(result>0 && process.frame)process.paused=false;
#endif
    xSemaphoreGiveRecursive(console_gate);return result;
}
extern "C" bool sk_shutdown_ready() {
    if(xSemaphoreTakeRecursive(console_gate,pdMS_TO_TICKS(20))!=pdTRUE)return false;
    bool ready=true;
    Console *entries[3];unsigned count=console_entries(entries);
    for(unsigned i=0;i<count;++i) {
        auto *c=entries[i];
        if(c->shell && c->shutdown_generation!=sk_power_generation())ready=false;
        if(c->dispatching || c->frame)ready=false;
        for(auto *f:c->retained)if(f)ready=false;
    }
#if SK_BACKGROUND_JOBS
    if(process.frame || executing_job)ready=false;
    for(auto &j:script_jobs)if(j.file || j.pending || j.console.shell)ready=false;
#endif
    xSemaphoreGiveRecursive(console_gate);return ready;
}
static void shutdown_console_poll() {
    if(!sk_power_cleaning() || active().shutdown_generation==sk_power_generation())return;
#if SK_BACKGROUND_JOBS
    // A Python finally block must finish before its app/frame is destroyed.
    if(process.frame && !process.done)return;
#endif
    close_all_apps();active().shutdown_generation=sk_power_generation();
}
