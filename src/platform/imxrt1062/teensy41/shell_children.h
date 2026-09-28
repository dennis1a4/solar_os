// Included by either the single USB console or the dual-console adapter.
// Each session retains its app contexts/TUIs while a child runs; frames live
// in PSRAM, not on the console stack.
struct AppFrame {
    AppFrame *parent;
    const solar_os_app_t *app;
    solar_os_tui_t *tui;
    solar_os_context_t context;
    int argc;
    char argv[SOLAR_OS_APP_ARG_MAX][SOLAR_OS_APP_ARG_LEN];
};
#if !SK_LCD_CONSOLE
static AppFrame *app_frame;
#endif
static solar_os_context_t *current_context() {
    return app_frame ? &app_frame->context : &shell_context;
}
static void restore_arguments(AppFrame *frame) {
    frame->context.argc = frame->argc;
    memcpy(frame->context.argv, frame->argv, sizeof(frame->argv));
}
static void select_frame() {
    foreground = app_frame ? app_frame->app : nullptr;
    active_tui = app_frame ? app_frame->tui : nullptr;
    solar_os_shell_session_set_foreground_app(session, foreground);
}
static void finish_app() {
    auto *finished = app_frame;
    if (!finished) return;
    const bool had_screen = active_tui != nullptr;
    // Failed starts may already have released their cold state.
    if (!foreground->state_slot || *foreground->state_slot)
        solar_os_app_stop(foreground, &finished->context);
    if (!finished->parent) {
        int exit_code = 0;
        char message[SOLAR_OS_CONTEXT_STATUS_MESSAGE_MAX] = {};
        const bool has_result = solar_os_context_take_exit_result(&finished->context, &exit_code);
        const bool has_message = solar_os_context_take_status_message(&finished->context, message, sizeof(message));
        if (has_result || has_message)
            solar_os_shell_session_set_exit_result(session, exit_code, has_message ? message : nullptr);
    }
    solar_os_app_registry_release(foreground, owner);
    app_frame = finished->parent;
    solar_os_memory_free(finished);
    if (had_screen || app_frame)
        solar_os_shell_io_clear(solar_os_shell_session_io(session));
    select_frame();
    if (app_frame) {
        restore_arguments(app_frame);
        if (active_tui) solar_os_tui_attach_session(active_tui);
        if (foreground->resume) foreground->resume(current_context());
    } else {
        solar_os_context_set_app_class(&shell_context, SOLAR_OS_APP_CLASS_TUI);
        solar_os_shell_session_prompt(&shell_context, session);
    }
}
static void launch_error(const char *message) {
    solar_os_shell_io_writeln(solar_os_shell_session_io(session), message);
    if (app_frame) {
        restore_arguments(app_frame);
        if (foreground->resume) foreground->resume(current_context());
    } else solar_os_shell_session_prompt(&shell_context, session);
}
static void service_requests() {
    if (solar_os_context_take_exit_request(current_context())) {
        if (app_frame) finish_app();
        else solar_os_shell_session_start(&shell_context, session,
            solar_os_shell_session_io(session), false, false);
    }
    auto *ctx = current_context();
    const auto *app = solar_os_context_take_launch_request(ctx);
    if (!app) return;
    if (app->app_class == SOLAR_OS_APP_CLASS_GUI && !solar_os_context_gfx(ctx)) {
        launch_error("This graphical app needs the local LCD console."); return;
    }
#if SK_LCD_CONSOLE
    if (!sk_app_allowed(app)) { launch_error("Audio is in use by another session."); return; }
#endif
    const auto policy = solar_os_context_take_launch_policy(ctx);
    unsigned depth = 0;
    for (auto *frame = app_frame; frame; frame = frame->parent) {
        ++depth;
        if (frame->app == app) { launch_error("That app is already active."); return; }
    }
    if (depth >= 4 || (app_frame && policy == SOLAR_OS_LAUNCH_CHILD_RETURN &&
        !(foreground->flags & SOLAR_OS_APP_FLAG_RESUMABLE))) {
        launch_error("This application cannot launch another child."); return;
    }
    auto *child = static_cast<AppFrame *>(solar_os_memory_calloc(1, sizeof(AppFrame),
        SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "usb-app-frame"));
    if (!child) { launch_error("Not enough memory to launch app."); return; }
    esp_err_t err = solar_os_app_registry_claim(app, owner, nullptr, 0);
    if (err != ESP_OK) {
        solar_os_memory_free(child); launch_error(esp_err_to_name(err)); return;
    }
    child->app = app;
    child->context = *ctx;
    child->argc = ctx->argc;
    memcpy(child->argv, ctx->argv, sizeof(child->argv));
    if (app_frame) {
        restore_arguments(app_frame);
        app_frame->tui = active_tui;
        if (policy == SOLAR_OS_LAUNCH_CHILD_RETURN) {
            if (foreground->suspend) foreground->suspend(ctx);
            child->parent = app_frame;
        } else {
            child->parent = app_frame->parent;
            solar_os_app_stop(foreground, ctx);
            solar_os_app_registry_release(foreground, owner);
            solar_os_memory_free(app_frame);
        }
    }
    app_frame = child;
    select_frame();
    err = solar_os_app_start(app, current_context());
    child->tui = active_tui;
    const bool finished = solar_os_context_take_exit_request(current_context());
    if (err != ESP_OK || finished) finish_app();
}
