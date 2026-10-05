// Included after shell_children.h. All state changes run in the owning console
// task under console_gate; other consoles may only enqueue a bounded request.
static void restore_app() {
    select_frame();
    if(!app_frame)return;
    restore_arguments(app_frame);
    if(active_tui)solar_os_tui_attach_session(active_tui);
    if(foreground->resume)foreground->resume(current_context());
}
static bool suspend_app() {
    if(!app_frame)return false;
    auto *io=solar_os_shell_session_io(session);
    if(!(foreground->flags & SOLAR_OS_APP_FLAG_RESUMABLE) || !foreground->resume) {
        solar_os_shell_io_writeln(io,"session: this app cannot be suspended");return false;
    }
    if(active().retained[3]) {
        solar_os_shell_io_writeln(io,"session: four suspended apps already; close or resume one first");return false;
    }
#if SK_BACKGROUND_JOBS
    if(process.frame==app_frame && (app_frame->parent || solar_os_context_graphics_active(&app_frame->context))) {
        solar_os_shell_io_writeln(io,"python: detach requires a standalone text script; graphics/child sessions remain foreground");return false;
    }
#endif
    app_frame->tui=active_tui;
    if(foreground->suspend)foreground->suspend(current_context());
    for(unsigned i=3;i>0;--i)active().retained[i]=active().retained[i-1];
    active().retained[0]=app_frame;
    const uint32_t id=app_frame->id;
    app_frame=nullptr;select_frame();
    session_text_mode();
    solar_os_shell_io_clear(io);
    solar_os_shell_io_write(io,"\033[?25h");
    solar_os_shell_io_printf(io,"Suspended session %lu. Use fg to resume.\n",(unsigned long)id);
    solar_os_shell_session_prompt(&shell_context,session);
    return true;
}
static AppFrame *take_retained(unsigned slot) {
    AppFrame *frame=active().retained[slot];
    for(unsigned i=slot;i<3;++i)active().retained[i]=active().retained[i+1];
    active().retained[3]=nullptr;
    return frame;
}
static void destroy_chain() {
    while(app_frame) {
        select_frame();
        auto *old=app_frame;
        if(!foreground->state_slot || *foreground->state_slot)
            solar_os_app_stop(foreground,current_context());
        solar_os_app_registry_release(foreground,owner);
        app_frame=old->parent;solar_os_memory_free(old);
    }
    select_frame();
}
static void close_all_apps() {
#if SK_HW_RESOURCES
    sk_hardware_console_release();
#endif
    active().quiet=true;
#if SK_BACKGROUND_JOBS
    if(app_frame && process_disconnect_frame(app_frame))app_frame=nullptr;
#endif
    destroy_chain();
    for(unsigned i=0;i<4;++i) {
        app_frame=active().retained[i];active().retained[i]=nullptr;
#if SK_BACKGROUND_JOBS
        if(app_frame && process_disconnect_frame(app_frame))app_frame=nullptr;
#endif
        destroy_chain();
    }
    active().quiet=false;active().request_action=0;
}
// Opt-in background timers run in the owning console under its gate. Never
// switch the active frame/TUI; retained callbacks must keep their UI suspended.
static void background_chain_tick(AppFrame *frame, uint32_t now) {
    for (; frame; frame=frame->parent) {
        const auto *app=frame->app;
        if (!(app->flags & SOLAR_OS_APP_FLAG_BACKGROUND_TICKS) || !app->event ||
            now-frame->background_tick < solar_os_app_tick_interval_ms(app,25)) continue;
        frame->background_tick=now;
        solar_os_event_t tick{}; tick.type=SOLAR_OS_EVENT_TICK; tick.data.tick_ms=now;
        app->event(&frame->context,&tick);
    }
}
static void service_background_apps(uint32_t now) {
    for (auto *frame:active().retained) background_chain_tick(frame,now);
    if (app_frame) background_chain_tick(app_frame->parent,now);
}
static void service_session_request() {
    const unsigned action=active().request_action;
    const uint32_t id=active().request_id;
    active().request_action=0;
    if(!action)return;
#if SK_BACKGROUND_JOBS
    if(action==3){process_attach();return;}
#endif
    auto *io=solar_os_shell_session_io(session);
    int slot=-1;
    for(unsigned i=0;i<4;++i)
        if(active().retained[i] && active().retained[i]->id==id)slot=int(i);
    if(action==1) {
        if(slot<0 || app_frame || !solar_os_shell_session_is_idle(session)) { solar_os_shell_io_writeln(io,"fg: session changed before request was handled");return; }
        app_frame=take_retained(unsigned(slot));
        solar_os_shell_io_clear(io);restore_app();
    } else if(action==2) {
        if(slot<0 && (!app_frame || app_frame->id!=id))return;
        AppFrame *saved=slot>=0?app_frame:nullptr;
        if(slot>=0)app_frame=take_retained(unsigned(slot));
        active().quiet=true;destroy_chain();active().quiet=false;
        app_frame=saved;
        solar_os_shell_io_clear(io);
        if(app_frame)restore_app();
        else {
            session_text_mode();
            solar_os_shell_io_write(io,"\033[?25h");
            solar_os_shell_io_printf(io,"Closed session %lu.\n",(unsigned long)id);
            solar_os_shell_session_prompt(&shell_context,session);
        }
    }
}
static bool console_has_audio(const Console &c) {
    for(auto *f=c.frame;f;f=f->parent)if(audio_app(f->app))return true;
    for(auto *head:c.retained)
        for(auto *f=head;f;f=f->parent)if(audio_app(f->app))return true;
    return false;
}
#if SK_AUDIO_PLAYER
extern "C" bool sk_console_recorder_busy() {
    auto has_recorder=[](const Console &c) {
        for(auto *f=c.frame;f;f=f->parent)if(!strcmp(f->app->name,"arecord"))return true;
        for(auto *head:c.retained)
            for(auto *f=head;f;f=f->parent)if(!strcmp(f->app->name,"arecord"))return true;
        return false;
    };
    for(auto &c:consoles)if(has_recorder(c))return true;
#if SK_TELNETD
    if(has_recorder(remote_console))return true;
#endif
    return false;
}
// Called by shell diagnostics while holding console_gate, including retained apps.
extern "C" bool sk_console_audio_busy() {
    for(auto &c:consoles)if(console_has_audio(c))return true;
#if SK_TELNETD
    if(console_has_audio(remote_console))return true;
#endif
    return false;
}
#endif
static unsigned console_entries(Console **entries) {
    entries[0]=&consoles[0];entries[1]=&consoles[1];
#if SK_TELNETD
    entries[2]=&remote_console;return 3;
#else
    return 2;
#endif
}
static bool parse_app_id(const char *text,uint32_t &id) {
    if(!text || !*text)return false;
    uint32_t value=0;
    for(;*text;++text) {
        if(*text<'0' || *text>'9' || value>(UINT32_MAX-unsigned(*text-'0'))/10)return false;
        value=value*10+unsigned(*text-'0');
    }
    if(value<4)return false;
    id=value;return true;
}
static void request_app(solar_os_context_t *ctx,int argc,char **argv,unsigned action) {
    auto *io=solar_os_context_shell_io(ctx);
    uint32_t id=0;
    if(argc==1 && action==1 && active().retained[0])id=active().retained[0]->id;
    else if(argc!=2 || !parse_app_id(argv[1],id)) {
        solar_os_shell_io_writeln(io,action==1?"usage: fg [app-session-id]; no suspended app if omitted":"usage: close <app-session-id>; discards unsaved state");return;
    }
#if SK_BACKGROUND_JOBS
    if(action==2 && process_close_request(id)) {solar_os_shell_io_writeln(io,"python: stop requested");return;}
    if(action==1 && process_foreground_request(id))return;
#endif
    Console *entries[3];const unsigned count=console_entries(entries);
    for(unsigned i=0;i<count;++i) {
        Console &c=*entries[i];
        bool retained=false;
        for(auto *f:c.retained)retained|=f && f->id==id;
        if(!retained && (!c.frame || c.frame->id!=id))continue;
        if(c.request_action) { solar_os_shell_io_writeln(io,"session: owner already has a pending request");return; }
        if(action==1 && (!retained || c.frame ||
            (&c!=&active() && (c.dispatching || !solar_os_shell_session_is_idle(c.shell))))) {
            solar_os_shell_io_writeln(io,"fg: owner must be at its shell; suspend the current app first");return;
        }
        c.request_id=id;c.request_action=action;
        solar_os_shell_io_printf(io,"Queued %s session %lu on %s.\n",action==1?"resume":"close",(unsigned long)id,c.name);
        return;
    }
    solar_os_shell_io_writeln(io,"session: no such app session");
}
extern "C" void sk_shell_cmd_fg(solar_os_context_t *ctx,int argc,char **argv) {
#if SK_BACKGROUND_JOBS
    if(argc==1 && !active().retained[0] && process.frame && process.detached) {process_foreground_request(process.frame->id);return;}
#endif
    request_app(ctx,argc,argv,1);
}
extern "C" void sk_shell_cmd_close(solar_os_context_t *ctx,int argc,char **argv) { request_app(ctx,argc,argv,2); }
static void list_app(solar_os_shell_io_t *io,const Console &c,AppFrame *head,const char *state) {
    if(!head)return;
    unsigned depth=0;for(auto *f=head;f;f=f->parent)++depth;
    solar_os_shell_io_printf(io,"%lu  %-14s %-9s %-16s depth=%u\n",(unsigned long)head->id,c.name,state,head->app->name,depth);
}
extern "C" void sk_shell_cmd_session(solar_os_context_t *ctx,int argc,char **argv) {
    if(argc>=2 && (!strcmp(argv[1],"fg") || !strcmp(argv[1],"foreground") || !strcmp(argv[1],"switch"))) {
        sk_shell_cmd_fg(ctx,argc-1,argv+1);return;
    }
    if(argc>=2 && !strcmp(argv[1],"close")) { sk_shell_cmd_close(ctx,argc-1,argv+1);return; }
    auto *io=solar_os_context_shell_io(ctx);
    if(argc>2 || (argc==2 && strcmp(argv[1],"list"))) {
        solar_os_shell_io_writeln(io,"usage: session [list] | session fg [ID] | session close ID");
        solar_os_shell_io_writeln(io,"Ctrl+Z suspends a resumable app; close discards its unsaved state.");return;
    }
    solar_os_shell_io_writeln(io,"ID Owner          State     App              Directory / chain");
    Console *entries[3];const unsigned count=console_entries(entries);
    for(unsigned i=0;i<count;++i) {
        Console &c=*entries[i];char path[SOLAR_OS_STORAGE_PATH_MAX]="-";
        if(c.shell)solar_os_shell_resolve_path(&c.context,nullptr,path,sizeof(path));
        bool online=c.local || (i==0 && bool(Serial));
#if SK_TELNETD
        if(i==2)online=sk_telnet_connected();
#endif
        solar_os_shell_io_printf(io,"%u  %-14s %-9s %-16s %s%s\n",i+1,c.name,
            online?"attached":"offline",c.app?c.app->name:"shell",path,&c==&active()?" *":"");
        list_app(io,c,c.frame,"active");
        for(auto *f:c.retained)list_app(io,c,f,"suspended");
    }
}
