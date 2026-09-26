/* Render the real calculator's serial output, including ANSI cursor motion.
 * The host terminal's height deliberately differs from the port's 24 rows. */
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "solar_os.h"
#include "solar_os_calc.h"
#include "solar_os_keys.h"
#include "solar_os_log.h"
#include "solar_os_memory.h"
#include "solar_os_shell_io.h"
#include "solar_os_storage.h"

#define WIDTH 100
#define HEIGHT_MAX 64
static char screen[HEIGHT_MAX][WIDTH + 1];
static size_t row, col, height;
static void blank(size_t y) { memset(screen[y], ' ', WIDTH); screen[y][WIDTH] = 0; }
static size_t clamp(size_t value, size_t limit) { return value < limit ? value : limit - 1; }
static void newline(void) {
    if (++row < height) return;
    for (size_t y = 1; y < height; ++y) memcpy(screen[y - 1], screen[y], WIDTH + 1);
    row = height - 1;
    blank(row);
}
static void render(const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = data[i];
        if (ch == 27) {
            assert(++i < length && data[i] == '[');
            unsigned values[2] = {0, 0};
            unsigned index = 0;
            while (++i < length && (isdigit(data[i]) || data[i] == ';')) {
                if (data[i] == ';') { assert(index == 0); ++index; }
                else values[index] = values[index] * 10 + data[i] - '0';
            }
            assert(i < length);
            switch (data[i]) {
            case 'H':
                row = clamp(values[0] ? values[0] - 1 : 0, height);
                col = clamp(values[1] ? values[1] - 1 : 0, WIDTH);
                break;
            case 'C': col = clamp(col + (values[0] ? values[0] : 1), WIDTH); break;
            case 'K': assert(values[0] == 0); memset(screen[row] + col, ' ', WIDTH - col); break;
            case 'J': assert(values[0] == 2); for (size_t y = 0; y < height; ++y) blank(y); break;
            default: assert(!"unexpected terminal sequence in cursor test");
            }
        } else if (ch == '\r') col = 0;
        else if (ch == '\n') newline();
        else if (ch == '\b') { if (col) --col; }
        else {
            assert(ch >= 32);
            screen[row][col] = ch;
            if (++col == WIDTH) { col = 0; newline(); }
        }
    }
}
bool solar_os_port_handle_valid(const solar_os_port_handle_t *handle) { return handle && handle->index == 0; }
esp_err_t solar_os_port_write(const solar_os_port_handle_t *handle, const uint8_t *data,
                               size_t length, size_t *written) {
    assert(solar_os_port_handle_valid(handle));
    render(data, length); *written = length; return ESP_OK;
}
void *solar_os_memory_calloc(size_t count, size_t size, solar_os_memory_class_t kind, const char *tag) {
    (void)kind; (void)tag; return calloc(count, size);
}
void solar_os_memory_free(void *ptr) { free(ptr); }
esp_err_t solar_os_log_write(solar_os_log_level_t level, const char *tag, const char *format, ...) {
    (void)level; (void)tag; (void)format; return ESP_OK;
}
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test error"; }
esp_err_t solar_os_storage_default_path(const char *relative, char *out, size_t len) {
    (void)relative; (void)out; (void)len; return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t solar_os_storage_sync_file(FILE *file) { (void)file; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t solar_os_shell_resolve_path(solar_os_context_t *ctx, const char *arg, char *out, size_t len) {
    (void)ctx; (void)arg; (void)out; (void)len; return ESP_ERR_NOT_SUPPORTED;
}
static void key(solar_os_context_t *ctx, char ch) {
    solar_os_event_t event = {.type = SOLAR_OS_EVENT_CHAR, .data.ch = ch};
    assert(solar_os_calc_app.event(ctx, &event));
}
static void check_calculator(size_t terminal_height, unsigned previous_lines) {
    height = terminal_height; row = col = 0;
    for (size_t y = 0; y < HEIGHT_MAX; ++y) blank(y);
    solar_os_context_t ctx;
    solar_os_context_init(&ctx, NULL, NULL);
    solar_os_shell_io_t io;
    solar_os_port_handle_t port = {.index = 0, .token = 1};
    solar_os_shell_io_init_port(&io, &port, 80, 24);
    solar_os_shell_io_set_terminal_profile(&io, SOLAR_OS_SHELL_TERMINAL_PROFILE_VT100);
    solar_os_context_set_shell_io(&ctx, &io);
    for (unsigned i = 0; i < previous_lines; ++i) solar_os_shell_io_writeln(&io, "earlier output");
    solar_os_shell_io_writeln(&io, "user@teensy41:/ calc");
    char *args[] = {"calc"};
    assert(solar_os_context_request_launch(&ctx, &solar_os_calc_app, 1, args) == ESP_OK);
    assert(solar_os_context_take_launch_request(&ctx) == &solar_os_calc_app);
    assert(solar_os_app_start(&solar_os_calc_app, &ctx) == ESP_OK);
    size_t input_row = row;
    assert(strncmp(screen[input_row], "> ", 2) == 0);
    key(&ctx, '5'); key(&ctx, '*'); key(&ctx, '8'); key(&ctx, '\b'); key(&ctx, '7');
    assert(row == input_row);
    assert(strncmp(screen[input_row], "> 5*7 ", 6) == 0);
    assert(strncmp(screen[input_row - 1], "SolarOS calculator", 18) == 0);
    assert(strncmp(screen[input_row - 2], "user@teensy41:/ calc", 19) == 0);
    key(&ctx, (char)SOLAR_OS_KEY_LEFT);
    assert(col == 4 && row == input_row);
    key(&ctx, (char)SOLAR_OS_KEY_RIGHT);
    assert(col == 5 && row == input_row);
    key(&ctx, '\r');
    assert(strncmp(screen[row - 1], "35 ", 3) == 0);
    assert(strncmp(screen[row - 2], "> 5*7 ", 6) == 0);
    assert(strncmp(screen[row - 3], "SolarOS calculator", 18) == 0);
    assert(strncmp(screen[row], "> ", 2) == 0);
    // Verify editing after the terminal has scrolled, as well as at launch.
    key(&ctx, '2'); key(&ctx, '+'); key(&ctx, '3'); key(&ctx, '\r');
    assert(strncmp(screen[row - 1], "5 ", 2) == 0);
    assert(strncmp(screen[row - 2], "> 2+3 ", 6) == 0);
    solar_os_app_stop(&solar_os_calc_app, &ctx);
}
static void check_redraw_modes(void) {
    height = 40; row = col = 0;
    for (size_t y = 0; y < HEIGHT_MAX; ++y) blank(y);
    solar_os_shell_io_t io;
    solar_os_port_handle_t port = {.index = 0, .token = 1};
    solar_os_shell_io_init_port(&io, &port, 80, 24);
    solar_os_shell_io_set_terminal_profile(&io, SOLAR_OS_SHELL_TERMINAL_PROFILE_VT100);
    solar_os_shell_io_writeln(&io, "keep this header");
    solar_os_shell_io_write(&io, "old input");
    assert(solar_os_shell_io_redraw_line(&io, 1, 0, "new", 3, 0) == ESP_OK);
    assert(row == 1 && col == 0 && strncmp(screen[1], "new ", 4) == 0);
    assert(strncmp(screen[0], "keep this header", 16) == 0);
    // A screen-oriented caller explicitly targets a different row.
    assert(solar_os_shell_io_redraw_line(&io, 4, 2, "field", 5, 3) == ESP_OK);
    assert(row == 4 && col == 5 && strncmp(screen[4], "  field ", 8) == 0);
}
int main(void) {
    check_redraw_modes();
    const size_t heights[] = {16, 24, 26, 40, 60};
    for (size_t i = 0; i < sizeof(heights) / sizeof(heights[0]); ++i) {
        check_calculator(heights[i], 0);
        check_calculator(heights[i], 25);
        check_calculator(heights[i], 70);
    }
    puts("Teensy calculator screen: typing, backspace, arrows, results and scrolling passed at 5 terminal heights");
}
