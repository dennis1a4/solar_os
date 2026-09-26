#include <arduino_freertos.h>
#include <queue.h>
#include "board.h"
#include "platform.h"
extern "C" {
#include "solar_os.h"
#include "solar_os_queue.h"
#include "solar_os_shell_parse.h"
#include "solar_os_expr.h"
#include "solar_os_memory.h"
}

static volatile uint32_t heartbeat_count;
static QueueHandle_t heartbeat_queue;
static solar_os_context_t context;
static esp_err_t output(const char *text, size_t length, void *) {
    sk_console_write(text, length);
    sk_display_write(text, length);
    return ESP_OK;
}
static void emit(const char *text) { output(text, strlen(text), nullptr); }

// First app exercises the upstream app lifecycle and expression engine.
// This is a command adapter, not the upstream interactive calculator UI.
static esp_err_t calc_start(solar_os_context_t *ctx) {
    solar_os_expr_program_t program;
    solar_os_expr_error_t error{};
    double result;
    esp_err_t status = solar_os_expr_compile(solar_os_context_argv(ctx, 0), &program, &error);
    if (status == ESP_OK) status = solar_os_expr_evaluate(&program, nullptr, &result, &error);
    char text[192];
    if (status == ESP_OK) snprintf(text, sizeof(text), "%.12g\r\n", result);
    else snprintf(text, sizeof(text), "Expression error at %u: %s\r\n", unsigned(error.position), error.message);
    output(text, strlen(text), nullptr);
    solar_os_context_finish(ctx, status == ESP_OK ? 0 : 1, nullptr);
    return status;
}
static const solar_os_app_t calculator = {
    .name = "calc", .summary = "Evaluate a SolarOS expression",
    .app_class = SOLAR_OS_APP_CLASS_COMMAND, .flags = 0, .start = calc_start,
};
static void run_calc(char *expression) {
    solar_os_context_init(&context, nullptr, nullptr);
    solar_os_context_set_output_handler(&context, output, nullptr);
    char *args[] = {expression};
    const esp_err_t err = solar_os_context_request_launch(&context, &calculator, 1, args);
    if (err != ESP_OK) { sk_console_printf("calc: %s\r\n", esp_err_to_name(err)); return; }
    const auto *app = solar_os_context_take_launch_request(&context);
    solar_os_app_start(app, &context);
    solar_os_app_stop(app, &context);
}
static void psram_test() {
    constexpr size_t count = 1024;
    auto *data = static_cast<uint32_t *>(solar_os_memory_alloc(count * sizeof(uint32_t),
        SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "psram-test"));
    if (!data) { emit("PSRAM allocation failed (no internal fallback)\r\n"); return; }
    for (size_t i = 0; i < count; ++i) data[i] = 0xa5a50000U ^ i;
    // Force cache writeback/invalidation before verifying actual external RAM.
    arm_dcache_flush_delete(data, count * sizeof(uint32_t));
    bool ok = true;
    for (size_t i = 0; i < count; ++i) if (data[i] != (0xa5a50000U ^ i)) ok = false;
    solar_os_memory_free(data);
    emit(ok ? "PSRAM 4 KiB test passed\r\n" : "PSRAM test FAILED\r\n");
}
static void command(char *line) {
    char *args[8];
    auto parsed = solar_os_shell_tokenize(line, args, 8);
    if (parsed.error != SOLAR_OS_SHELL_PARSE_OK) {
        sk_console_printf("Parse error: %s\r\n", solar_os_shell_parse_error_text(parsed.error)); return;
    }
    if (!parsed.argc) return;
    if (!strcmp(args[0], "help")) {
        emit("SolarOS Teensy bring-up console (full shell integration pending)\r\n"
             "help | info | mem | psram | mount | sdinfo | ls [path] | cat path\r\n"
             "slots | i2c BUS | calc \"EXPRESSION\" | tone on|off\r\n");
    } else if (!strcmp(args[0], "info")) {
        uint32_t tick = 0;
        xQueuePeek(heartbeat_queue, &tick, 0);
        sk_console_printf("i.MX RT1062 / Teensy 4.1 / FreeRTOS %s\r\n"
                          "Uptime=%lu ms heartbeat=%lu stack-free=%lu words\r\n",
            tskKERNEL_VERSION_NUMBER, (unsigned long)millis(), (unsigned long)tick,
            (unsigned long)uxTaskGetStackHighWaterMark(nullptr));
    } else if (!strcmp(args[0], "mem")) sk_memory_print();
    else if (!strcmp(args[0], "psram")) psram_test();
    else if (!strcmp(args[0], "mount")) sk_console_printf("SDIO: %s\r\n", esp_err_to_name(sk_sd_mount()));
    else if (!strcmp(args[0], "sdinfo")) sk_sd_print_status();
    else if (!strcmp(args[0], "ls")) sk_sd_list(parsed.argc > 1 ? args[1] : "/");
    else if (!strcmp(args[0], "cat") && parsed.argc == 2) sk_sd_cat(args[1]);
    else if (!strcmp(args[0], "slots")) sk_slots_print();
    else if (!strcmp(args[0], "calc") && parsed.argc == 2) run_calc(args[1]);
    else if (!strcmp(args[0], "i2c") && parsed.argc == 2 && strlen(args[1]) == 1 && args[1][0] >= '0' && args[1][0] <= '2') {
        unsigned bus = args[1][0] - '0';
        for (uint8_t address = 8; address < 0x78; ++address) {
            if (sk_i2c_transfer(bus, address, nullptr, 0, nullptr, 0) == ESP_OK)
                sk_console_printf("I2C%u: 0x%02x\r\n", bus, address);
            vTaskDelay(1);
        }
    } else if (!strcmp(args[0], "tone") && parsed.argc == 2 &&
        (!strcmp(args[1], "on") || !strcmp(args[1], "off"))) sk_audio_tone(!strcmp(args[1], "on"));
    else emit("Unknown command or arguments; use help\r\n");
}
static void heartbeat(void *) {
    while (true) {
        const uint32_t count = ++heartbeat_count;
        xQueueOverwrite(heartbeat_queue, &count);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
static void shell_task(void *) {
    sk_console_begin();
    sk_memory_begin();
    emit("\r\nSolarOS / SuperKeyboard CPU v2 / Teensy 4.1\r\nFreeRTOS console task running\r\n");
    if (sk_buses_begin() != ESP_OK) {
        emit("Bus initialization failed; stopped\r\n");
        vTaskSuspend(nullptr);
    }
    sk_console_printf("SDIO: %s\r\n", esp_err_to_name(sk_sd_mount()));
    sk_memory_print();
    sk_displays_begin();
    sk_audio_begin();
    sk_usb_begin();
#if SK_ETHERNET
    extern void sk_network_begin();
    sk_network_begin();
#endif
#if SK_UPSTREAM_SHELL
    extern void sk_upstream_shell_run();
    sk_upstream_shell_run();
#else
    emit("Bring-up console; type help\r\nsolaros[teensy41]> ");
    char line[160]; size_t used = 0;
    bool was_cr = false, overflow = false;
    while (true) {
        sk_usb_poll();
        int ch = sk_console_read();
        if (ch < 0) { vTaskDelay(pdMS_TO_TICKS(2)); continue; }
        if (ch == '\n' && was_cr) { was_cr = false; continue; }
        was_cr = ch == '\r';
        if (ch == 3) { used = 0; overflow = false; emit("^C\r\nsolaros[teensy41]> "); }
        else if (ch == '\r' || ch == '\n') {
            emit("\r\n");
            line[used] = 0;
            if (overflow) emit("Line too long; discarded\r\n");
            else command(line);
            used = 0; overflow = false;
            emit("solaros[teensy41]> ");
        } else if (ch == 8 || ch == 127) {
            if (used && !overflow) { --used; emit("\b \b"); }
        } else if (ch >= 32 && ch < 127) {
            if (used < sizeof(line) - 1 && !overflow) {
                line[used++] = char(ch); char echo = ch; output(&echo, 1, nullptr);
            } else overflow = true;
        }
    }
#endif
}
void setup() {
    // No LED heartbeat: pin 13 is the primary display's SPI clock.
    digitalWrite(superkeyboard::motor_enable, LOW); pinMode(superkeyboard::motor_enable, OUTPUT);
    digitalWrite(superkeyboard::relay, LOW); pinMode(superkeyboard::relay, OUTPUT);
    // LM4871 shutdown is active HIGH; keep the loudspeaker amplifier off.
    digitalWrite(superkeyboard::amplifier_shutdown, HIGH); pinMode(superkeyboard::amplifier_shutdown, OUTPUT);
    heartbeat_queue = solar_os_queue_create_internal(1, sizeof(uint32_t));
    configASSERT(heartbeat_queue);
    configASSERT(xTaskCreate(heartbeat, "heartbeat", 256, nullptr, 1, nullptr) == pdPASS);
    #if SK_AUDIO_PLAYER
    constexpr unsigned console_stack = 8192; // MP3 decoder scratch plus shell frames.
#elif SK_UPSTREAM_SHELL
    constexpr unsigned console_stack = 6144;
#else
    constexpr unsigned console_stack = 4096;
#endif
    configASSERT(xTaskCreate(shell_task, "solar-console", console_stack, nullptr, 2, nullptr) == pdPASS);
    vTaskStartScheduler();
    while (true) {}
}
void loop() {}
