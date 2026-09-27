#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
#include "solar_os.h"
#include "solar_os_shell.h"
#include "solar_os_app_registry.h"
#include "solar_os_memory.h"
#include "solar_os_tui.h"
}
static solar_os_context_t shell_context;
static solar_os_shell_session_t *session;
static const solar_os_app_t *foreground;
static solar_os_tui_t *active_tui;
static const char *owner = "test";
static solar_os_shell_io_t io;
static unsigned allocations, frees, starts, stops, resumes, prompts, clears;
static bool fail_alloc, fail_start;
static int last_exit_code;
static char last_exit_message[SOLAR_OS_CONTEXT_STATUS_MESSAGE_MAX];
extern "C" size_t strlcpy(char *out, const char *in, size_t n) {
    const size_t len = strlen(in);
    if (n) { const size_t k = len < n-1 ? len : n-1; memcpy(out, in, k); out[k] = 0; }
    return len;
}
extern "C" void *solar_os_memory_calloc(size_t n, size_t s, solar_os_memory_class_t, const char *) {
    if (fail_alloc) return nullptr;
    ++allocations; return calloc(n, s);
}
extern "C" void solar_os_memory_free(void *p) { if (p) { ++frees; free(p); } }
extern "C" esp_err_t solar_os_log_write(solar_os_log_level_t, const char *, const char *, ...) { return ESP_OK; }
extern "C" const char *esp_err_to_name(esp_err_t) { return "error"; }
extern "C" esp_err_t solar_os_app_registry_claim(const solar_os_app_t *, const char *, char *, size_t) { return ESP_OK; }
extern "C" void solar_os_app_registry_release(const solar_os_app_t *, const char *) {}
extern "C" solar_os_shell_io_t *solar_os_shell_session_io(solar_os_shell_session_t *) { return &io; }
extern "C" void solar_os_shell_session_set_foreground_app(solar_os_shell_session_t *, const solar_os_app_t *) {}
extern "C" void solar_os_shell_session_set_exit_result(solar_os_shell_session_t *, int code, const char *message) {
    last_exit_code=code; strlcpy(last_exit_message, message ? message : "", sizeof(last_exit_message));
}
extern "C" void solar_os_shell_session_prompt(solar_os_context_t *, solar_os_shell_session_t *) { ++prompts; }
extern "C" esp_err_t solar_os_shell_session_start(solar_os_context_t *, solar_os_shell_session_t *, solar_os_shell_io_t *, bool, bool) { return ESP_OK; }
extern "C" esp_err_t solar_os_shell_io_clear(solar_os_shell_io_t *) { ++clears; return ESP_OK; }
extern "C" esp_err_t solar_os_shell_io_writeln(solar_os_shell_io_t *, const char *) { return ESP_OK; }
extern "C" void solar_os_shell_io_capture_output(solar_os_shell_io_t *, solar_os_context_t *) {}
extern "C" void solar_os_tui_attach_session(solar_os_tui_t *tui) { active_tui = tui; }
#include "platform/imxrt1062/teensy41/shell_children.h"
static solar_os_tui_t parent_tui, child_tui;
static esp_err_t start_parent(solar_os_context_t *) { ++starts; active_tui = &parent_tui; return ESP_OK; }
static esp_err_t start_child(solar_os_context_t *) { ++starts; active_tui = &child_tui; return fail_start ? ESP_FAIL : ESP_OK; }
static esp_err_t start_command(solar_os_context_t *ctx) { solar_os_context_finish(ctx, 0, nullptr); return ESP_OK; }
static esp_err_t failed_command(solar_os_context_t *ctx) { solar_os_context_finish(ctx, 7, "login denied"); return ESP_OK; }
static void stop(solar_os_context_t *) { ++stops; active_tui = nullptr; }
static void resume(solar_os_context_t *ctx) {
    ++resumes;
    assert(active_tui == &parent_tui);
    assert(ctx->argc == 1 && !strcmp(ctx->argv[0], "original-parent-args"));
}
static void launch(const solar_os_app_t *app, const char *arg, solar_os_launch_policy_t policy) {
    char *args[] = {const_cast<char *>(arg)};
    assert(solar_os_context_request_launch_ex(current_context(), app, 1, args, policy) == ESP_OK);
    service_requests();
}
static void finish() { solar_os_context_finish(current_context(), 0, nullptr); service_requests(); }
int main() {
    solar_os_context_init(&shell_context, nullptr, nullptr);
    solar_os_app_t parent{};
    parent.name="parent"; parent.app_class=SOLAR_OS_APP_CLASS_TUI;
    parent.flags=SOLAR_OS_APP_FLAG_RESUMABLE;
    parent.start=start_parent; parent.stop=stop; parent.resume=resume;
    solar_os_app_t child{};
    child.name="child"; child.app_class=SOLAR_OS_APP_CLASS_TUI;
    child.start=start_child; child.stop=stop;
    launch(&parent, "original-parent-args", SOLAR_OS_LAUNCH_REPLACE);
    auto *parent_ctx=current_context();
    for (unsigned i=0; i<5; ++i) {
        launch(&child, "child-args", SOLAR_OS_LAUNCH_CHILD_RETURN);
        assert(foreground==&child && current_context()!=parent_ctx);
        finish();
        assert(foreground==&parent && current_context()==parent_ctx && active_tui==&parent_tui);
    }
    fail_alloc=true;
    launch(&child,"failed-child",SOLAR_OS_LAUNCH_CHILD_RETURN);
    fail_alloc=false;
    assert(foreground==&parent && current_context()==parent_ctx);
    fail_start=true;
    launch(&child,"failed-start",SOLAR_OS_LAUNCH_CHILD_RETURN);
    fail_start=false;
    assert(foreground==&parent);
    launch(&parent,"duplicate",SOLAR_OS_LAUNCH_CHILD_RETURN);
    assert(foreground==&parent);
    finish();
    assert(!foreground && !app_frame && allocations==frees && prompts==1);
    launch(&parent,"original-parent-args",SOLAR_OS_LAUNCH_REPLACE);
    launch(&child,"replacement",SOLAR_OS_LAUNCH_REPLACE);
    assert(foreground==&child && !app_frame->parent);
    finish();
    assert(allocations==frees && resumes==8);
    solar_os_app_t command{};
    command.name="command"; command.app_class=SOLAR_OS_APP_CLASS_COMMAND;
    command.start=start_command;
    const auto saved_clears=clears;
    launch(&command,"text output",SOLAR_OS_LAUNCH_REPLACE);
    assert(!foreground && allocations==frees && clears==saved_clears);
    command.start=failed_command;
    launch(&command,"failed command",SOLAR_OS_LAUNCH_REPLACE);
    assert(last_exit_code==7 && !strcmp(last_exit_message,"login denied"));
    assert(!foreground && allocations==frees && clears==saved_clears);
    puts("PASS: child return, retained arguments/context/TUI, failures, duplicate rejection, replace and cleanup");
}
