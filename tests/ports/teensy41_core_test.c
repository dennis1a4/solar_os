/* Host-only collaborators exercise the real, headless SolarOS core. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "solar_os.h"
#include "solar_os_memory.h"
#include "solar_os_log.h"
#include "solar_os_shell_io.h"
#include "solar_os_shell_parse.h"
#include "solar_os_expr.h"

static bool fail_allocation;
static unsigned allocations, releases;
static void *app_state;
static esp_err_t start_result;
void *solar_os_memory_calloc(size_t count, size_t size,
    solar_os_memory_class_t kind, const char *tag) {
    (void)kind; (void)tag;
    if (fail_allocation) return NULL;
    ++allocations;
    return calloc(count, size);
}
void solar_os_memory_free(void *ptr) { ++releases; free(ptr); }
esp_err_t solar_os_log_write(solar_os_log_level_t level, const char *tag,
    const char *format, ...) {
    (void)level; (void)tag; (void)format;
    return ESP_OK;
}
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test error"; }
void solar_os_shell_io_capture_output(solar_os_shell_io_t *io, solar_os_context_t *ctx) {
    (void)io; (void)ctx;
}
static esp_err_t start(solar_os_context_t *ctx) { (void)ctx; return start_result; }
int main(void) {
    solar_os_context_t ctx;
    solar_os_context_init(&ctx, NULL, NULL);
    solar_os_context_set_graphics_active(&ctx, true);
    assert(!solar_os_context_graphics_active(&ctx));
    solar_os_context_set_streaming_graphics_active(&ctx, true);
    assert(!solar_os_context_graphics_active(&ctx));
    solar_os_app_t app = {
        .name = "test", .app_class = SOLAR_OS_APP_CLASS_COMMAND,
        .start = start, .state_slot = &app_state, .state_size = 32,
        .state_storage = SOLAR_OS_APP_STATE_TRANSIENT,
    };
    char *args[] = {"hello"};
    assert(solar_os_context_request_launch(&ctx, &app, 1, args) == ESP_OK);
    assert(solar_os_context_take_launch_request(&ctx) == &app);
    assert(strcmp(solar_os_context_argv(&ctx, 0), "hello") == 0);
    assert(solar_os_app_start(&app, &ctx) == ESP_OK);
    assert(app_state && allocations == 1);
    solar_os_app_stop(&app, &ctx);
    assert(!app_state && releases == 1);

    solar_os_context_init(&ctx, NULL, NULL);
    fail_allocation = true;
    assert(solar_os_app_start(&app, &ctx) == ESP_ERR_NO_MEM);
    assert(ctx.exit_result_pending && ctx.exit_code == 1 && !app_state);
    fail_allocation = false;
    solar_os_context_init(&ctx, NULL, NULL);
    start_result = ESP_FAIL;
    assert(solar_os_app_start(&app, &ctx) == ESP_FAIL);
    assert(!app_state && allocations == releases);

    char line[] = "calc \"2 + 3 * 4\"";
    char *argv[8];
    solar_os_shell_parse_result_t parsed = solar_os_shell_tokenize(line, argv, 8);
    assert(parsed.error == SOLAR_OS_SHELL_PARSE_OK && parsed.argc == 2);
    solar_os_expr_program_t program;
    solar_os_expr_error_t error;
    double value;
    assert(solar_os_expr_compile(argv[1], &program, &error) == ESP_OK);
    assert(solar_os_expr_evaluate(&program, NULL, &value, &error) == ESP_OK);
    assert(value == 14.0);
    assert(solar_os_expr_compile("sqrt(81)", &program, &error) == ESP_OK);
    assert(solar_os_expr_evaluate(&program, NULL, &value, &error) == ESP_OK);
    assert(value == 9.0);
    puts("Teensy headless core: lifecycle, failure cleanup, parsing and expressions passed");
}
