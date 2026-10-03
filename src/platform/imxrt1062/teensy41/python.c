#if SK_UPSTREAM_SHELL
// One MicroPython VM. Workstation builds execute its lifecycle on an admitted worker.
#if SK_BACKGROUND_JOBS
#include "process_job.h"
#endif
#include <errno.h>
#if SK_ETHERNET
#include "network_socket.h"
void sk_python_solaros_net_init(void);
void sk_python_solaros_net_destroy(void);
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "solar_os_python.h"
#include "solar_os_shell.h"
#include "solar_os_shell_io.h"
#include "solar_os_memory.h"
#include "solar_os_keys.h"
#include "py/compile.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/repl.h"
#include "py/runtime.h"
#include "py/objlist.h"
#include "py/objstr.h"
#include "shared/runtime/gchelper.h"

#define PY_HEAP_BYTES (512U * 1024U)
#define PY_INPUT_BYTES 8192U
#if SK_GRAPHICS
void sk_python_gfx_init(solar_os_context_t *);
void sk_python_gfx_destroy(void);
#endif
#if SK_HW_RESOURCES
void sk_python_hardware_init(solar_os_context_t *);
void sk_python_hardware_destroy(void);
#endif
static solar_os_context_t *context;
static void *heap;
static char *source;
static size_t source_len, line_start;
static bool initialized;
extern bool sk_python_poll_cancel(void);
extern uint32_t sk_python_random_seed(void);

uint32_t solar_os_micropython_random_seed(void) { return sk_python_random_seed(); }
bool solar_os_micropython_stop_requested(void) { return sk_python_poll_cancel(); }
void solar_os_micropython_vm_hook(void) {
    static unsigned branches;
    if ((++branches & 0xff) == 0) {
        if (sk_python_poll_cancel()) mp_sched_keyboard_interrupt();
        if ((branches & 0x3fff) == 0) vTaskDelay(1);
    }
}
void mp_hal_stdout_tx_strn_cooked(const char *text, size_t len) {
    solar_os_shell_io_t *io = solar_os_context_shell_io(context);
    // The shell stream inserts CR before LF and tracks cursor movement.
    solar_os_shell_io_write_len(io, text, len);
}
int solar_os_micropython_resolve_path(const char *input, char *output, size_t size) {
    if (solar_os_shell_resolve_path(context, input, output, size) == ESP_OK) return 0;
    errno = ENAMETOOLONG;
    return -1;
}
void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}
void nlr_jump_fail(void *value) { (void)value; configASSERT(false); for (;;) vTaskDelay(1); }

// Reset the scan boundary for each synchronous entry from the app lifecycle.
// Use the task allocation boundary, not its historical high-water mark: a
// handled deep recursion must not shrink every later invocation's stack limit.
static void stack_boundary(void *top) {
    TaskStatus_t task;
    vTaskGetInfo(NULL, &task, pdFALSE, eInvalid);
    size_t bytes = (uintptr_t)top - (uintptr_t)task.pxStackBase;
    // Python's recursion guard does not bound native storage/network frames.
    // Keep room for LittleFS even when a script catches its recursion error
    // and performs file I/O before unwinding.
#if SK_QSPI_FLASH
    const size_t native_reserve = 8192;
#else
    const size_t native_reserve = 1024;
#endif
    configASSERT(bytes > native_reserve + 1024);
    mp_cstack_init_with_top(top, bytes - native_reserve);
}
static bool execute(const char *text, bool file, bool repl) {
    volatile uintptr_t top = 0;
    stack_boundary((void *)&top);
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lexer = file ? mp_lexer_new_from_file(qstr_from_str(text)) :
            mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, text, strlen(text), 0);
        qstr name = lexer->source_name;
        mp_parse_tree_t tree = mp_parse(lexer, repl ? MP_PARSE_SINGLE_INPUT : MP_PARSE_FILE_INPUT);
        mp_obj_t fun = mp_compile(&tree, name, repl);
        mp_call_function_0(fun);
        nlr_pop();
        return true;
    }
    mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
    return false;
}
#if SK_BACKGROUND_JOBS
static mp_obj_t process_input(size_t argc,const mp_obj_t *argv) {
    if(argc)mp_obj_print_helper(&mp_plat_print,argv[0],PRINT_STR);
    vstr_t line;vstr_init(&line,32);
    while(true) {
        int ch=sk_process_stdin();
        if(ch==-2){vstr_clear(&line);mp_raise_type(&mp_type_KeyboardInterrupt);}
        if(ch==4 && !line.len){vstr_clear(&line);mp_raise_type(&mp_type_EOFError);}
        if(ch=='\r' || ch=='\n'){mp_hal_stdout_tx_strn_cooked("\n",1);break;}
        if(ch==8 || ch==127){if(line.len){vstr_cut_tail_bytes(&line,1);mp_hal_stdout_tx_strn_cooked("\b \b",3);}}
        else if(ch>=32){if(line.len>=4096){vstr_clear(&line);mp_raise_ValueError(MP_ERROR_TEXT("input exceeds 4096 bytes"));}vstr_add_char(&line,ch);char c=ch;mp_hal_stdout_tx_strn_cooked(&c,1);}
    }
    return mp_obj_new_str_from_vstr(&line);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(sk_python_input_obj,0,1,process_input);
#endif
static void prompt(void) {
    solar_os_shell_io_write(solar_os_context_shell_io(context), source_len ? "... " : ">>> ");
}
static void python_stop_sync(solar_os_context_t *ctx) {
    (void)ctx;
#if SK_GRAPHICS
    sk_python_gfx_destroy();
#endif
    if (initialized) {
        volatile uintptr_t top = 0;
        stack_boundary((void *)&top);
        gc_sweep_all(); // Close even file objects still referenced by globals.
        #if SK_ETHERNET
        sk_python_solaros_net_destroy();
        sk_python_network_close_all();
        #endif
        #if SK_HW_RESOURCES
        sk_python_hardware_destroy();
        #endif
        mp_deinit();
        initialized = false;
    }
    solar_os_memory_free(source); source = NULL;
    solar_os_memory_free(heap); heap = NULL;
    context = NULL;
}
static esp_err_t python_start_sync(solar_os_context_t *ctx) {
    context = ctx;
    source_len = line_start = 0;
    heap = solar_os_memory_alloc(PY_HEAP_BYTES, SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "python-heap");
    source = solar_os_memory_alloc(PY_INPUT_BYTES, SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "python-input");
    if (!heap || !source) {
        solar_os_context_finish(ctx, 1, "python: needs 512 KiB free PSRAM");
        return ESP_ERR_NO_MEM;
    }
    source[0] = 0;
    volatile uintptr_t top = 0;
    stack_boundary((void *)&top);
    gc_init(heap, (uint8_t *)heap + PY_HEAP_BYTES);
    MP_STATE_VM(solar_os_active_parse_tree_chunk) = NULL;
    mp_init();
    initialized = true;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) != 0) {
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
        solar_os_context_finish(ctx, 1, NULL);
        return ESP_FAIL;
    }
    #if SK_ETHERNET
    sk_python_network_init();
    sk_python_solaros_net_init();
    #endif
    #if SK_GRAPHICS
    sk_python_gfx_init(ctx);
    #endif
    #if SK_HW_RESOURCES
    sk_python_hardware_init(ctx);
    #endif
    const int argc = solar_os_context_argc(ctx);
    const char *path = argc > 1 ? solar_os_context_argv(ctx, 1) : NULL;
    const bool command = path && !strcmp(path, "-c");
    if (command && argc < 3) {
        nlr_pop();
        solar_os_context_finish(ctx, 1, "usage: python [-c code | script.py [args...]]");
        return ESP_ERR_INVALID_ARG;
    }
    if (path && !command) {
        char resolved[SOLAR_OS_MICROPYTHON_PATH_MAX];
        if (solar_os_micropython_resolve_path(path, resolved, sizeof(resolved)) != 0) mp_raise_OSError(errno);
        char *slash = strrchr(resolved, '/');
        if (slash) mp_obj_list_append(mp_sys_path, mp_obj_new_str(resolved, slash - resolved + 1));
    }
    mp_obj_list_append(mp_sys_path, mp_obj_new_str("", 0));
    mp_obj_list_append(mp_sys_path, mp_obj_new_str("/", 1));
    mp_obj_list_append(mp_sys_path, mp_obj_new_str("/flash/lib", 10));
    mp_obj_list_append(mp_sys_path, mp_obj_new_str("/sd/lib", 7));
    for (int i = command ? 2 : 1; i < argc; ++i) {
        const char *arg = solar_os_context_argv(ctx, i);
        mp_obj_list_append(mp_sys_argv, mp_obj_new_str(arg, strlen(arg)));
    }
    nlr_pop();
    if (path) {
        bool ok = execute(command ? solar_os_context_argv(ctx, 2) : path, !command, false);
        solar_os_context_finish(ctx, ok ? 0 : 1, NULL);
    } else {
        solar_os_shell_io_writeln(solar_os_context_shell_io(ctx),
            "MicroPython on SolarOS / Teensy 4.1. Ctrl-C interrupt, Ctrl-D exit.");
        prompt();
    }
    return ESP_OK;
}
static bool python_event_sync(solar_os_context_t *ctx, const solar_os_event_t *event) {
    if (event->type != SOLAR_OS_EVENT_CHAR) return false;
    unsigned char ch = event->data.ch;
    solar_os_shell_io_t *io = solar_os_context_shell_io(ctx);
    if (ch == 4 || ch == SOLAR_OS_KEY_APP_EXIT) {
        solar_os_shell_io_writeln(io, "");
        solar_os_context_finish(ctx, 0, NULL);
    } else if (ch == 3 || ch == SOLAR_OS_KEY_ESCAPE) {
        source_len = line_start = 0; source[0] = 0;
        solar_os_shell_io_writeln(io, "\nKeyboardInterrupt"); prompt();
    } else if (ch == '\r' || ch == '\n') {
        solar_os_shell_io_writeln(io, "");
        source[source_len] = 0;
        if (source_len > line_start && mp_repl_continue_with_input(source)) {
            source[source_len++] = '\n'; source[source_len] = 0;
            line_start = source_len;
        } else {
            execute(source, false, true);
            source_len = line_start = 0; source[0] = 0;
        }
        prompt();
    } else if (ch == 8 || ch == 127) {
        if (source_len > line_start) {
            source[--source_len] = 0;
            solar_os_shell_io_write(io, "\b \b");
        }
    } else if (ch >= 32 && ch < 127 && source_len < PY_INPUT_BYTES - 2) {
        source[source_len++] = ch; source[source_len] = 0;
        solar_os_shell_io_put_char(io, ch);
    }
    return true;
}
#if SK_BACKGROUND_JOBS
static esp_err_t python_start(solar_os_context_t *ctx) {
    return sk_process_start(ctx,python_start_sync,python_event_sync,python_stop_sync);
}
#define python_stop sk_process_stop
#define python_event sk_process_event
#else
#define python_start python_start_sync
#define python_stop python_stop_sync
#define python_event python_event_sync
#endif
const solar_os_app_t solar_os_python_app = {
    .name = "python", .summary = "MicroPython REPL and SD scripts",
    .app_class = SOLAR_OS_APP_CLASS_TUI,
    .start = python_start, .stop = python_stop, .event = python_event,
#if SK_BACKGROUND_JOBS
    .flags = SOLAR_OS_APP_FLAG_RESUMABLE, .suspend = sk_process_suspend, .resume = sk_process_resume,
#endif
};
#endif
