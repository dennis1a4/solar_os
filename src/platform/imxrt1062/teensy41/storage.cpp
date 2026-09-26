#include <arduino_freertos.h>
#include <SD.h>
#include <semphr.h>
#include "platform.h"
static SemaphoreHandle_t mutex;
static bool mounted;
static constexpr unsigned mount_attempt_limit = 3;
static unsigned mount_attempts;
static uint32_t mount_elapsed_ms;
static uint8_t mount_errors[mount_attempt_limit];
static uint32_t mount_error_data[mount_attempt_limit];

esp_err_t sk_sd_mount() {
    if (!mutex) mutex = xSemaphoreCreateMutex(); // startup/shell task owns initialization
    if (!mutex) return ESP_ERR_NO_MEM;
    xSemaphoreTake(mutex, portMAX_DELAY);
    // BUILTIN_SDCARD selects native four-bit SDIO, not SPI or a GPIO CS.
    if (!mounted) {
        mount_attempts = 0;
        const uint32_t started = millis();
        // A failed startup previously recovered with a later manual mount.
        // Retry initialization a bounded number of times, yielding between
        // attempts so a missing card cannot cause an endless startup loop.
        do {
            mounted = SD.begin(BUILTIN_SDCARD);
            auto *card = SD.sdfs.card();
            mount_errors[mount_attempts] = card ? card->errorCode() : 0xff;
            mount_error_data[mount_attempts] = card ? card->errorData() : 0;
            ++mount_attempts;
            if (mounted || mount_attempts == mount_attempt_limit) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        } while (true);
        mount_elapsed_ms = millis() - started;
    }
    xSemaphoreGive(mutex);
    return mounted ? ESP_OK : ESP_ERR_NOT_FOUND;
}
void sk_sd_print_status() {
    // Only called by the application task, which owns SD initialization/I/O.
    sk_console_printf("SDIO: mounted=%s attempts=%u elapsed=%lu ms\r\n",
                      mounted ? "yes" : "no", mount_attempts,
                      (unsigned long)mount_elapsed_ms);
    for (unsigned i = 0; i < mount_attempts; ++i)
        sk_console_printf("  attempt %u: card-error=0x%02x data=0x%08lx\r\n",
                          i + 1, unsigned(mount_errors[i]),
                          (unsigned long)mount_error_data[i]);
}
void sk_sd_list(const char *path) {
    if (!mounted) { sk_console_print("SD not mounted; use mount\r\n"); return; }
    xSemaphoreTake(mutex, portMAX_DELAY);
    File dir = SD.open(path, FILE_READ);
    if (!dir || !dir.isDirectory()) sk_console_print("Directory not found\r\n");
    else {
        for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
            sk_console_printf("%c %10lu %s\r\n", entry.isDirectory() ? 'd' : '-',
                              (unsigned long)entry.size(), entry.name());
            entry.close();
            vTaskDelay(1);
        }
    }
    dir.close();
    xSemaphoreGive(mutex);
}
void sk_sd_cat(const char *path) {
    if (!mounted) { sk_console_print("SD not mounted; use mount\r\n"); return; }
    xSemaphoreTake(mutex, portMAX_DELAY);
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) sk_console_print("File not found\r\n");
    else {
        char buffer[128];
        while (file.available()) {
            if (sk_console_read() == 3) break;
            const int count = file.read(buffer, sizeof(buffer));
            if (count <= 0) break;
            sk_console_write(buffer, count);
            vTaskDelay(1);
        }
        sk_console_print("\r\n");
    }
    file.close();
    xSemaphoreGive(mutex);
}
