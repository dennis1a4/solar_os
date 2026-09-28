#include <arduino_freertos.h>
#include <Wire.h>
#include <SPI.h>
#include <semphr.h>
#include "board.h"
#include "platform.h"

static SemaphoreHandle_t i2c_mutex[3], spi_mutex[2], slot_mutex;
static TwoWire *const wires[] = {&Wire, &Wire1, &Wire2};
static SPIClass *const spis[] = {&SPI, &SPI1};
static HardwareSerial *const uarts[] = {&Serial7, &Serial8, &Serial3};
static char owners[3][24];
static bool uart_active[3];
static bool owns(unsigned slot, const char *owner) {
    return slot < 3 && owner && *owner && strcmp(owners[slot], owner) == 0;
}
esp_err_t sk_buses_begin() {
    slot_mutex = xSemaphoreCreateMutex();
    if (!slot_mutex) return ESP_ERR_NO_MEM;
    for (auto &m : i2c_mutex) if (!(m = xSemaphoreCreateMutex())) return ESP_ERR_NO_MEM;
    for (auto &m : spi_mutex) if (!(m = xSemaphoreCreateMutex())) return ESP_ERR_NO_MEM;
    Wire.setSDA(superkeyboard::audio_sda); Wire.setSCL(superkeyboard::audio_scl);
    Wire1.setSDA(superkeyboard::wire1_sda); Wire1.setSCL(superkeyboard::wire1_scl);
    Wire2.setSDA(superkeyboard::wire2_sda); Wire2.setSCL(superkeyboard::wire2_scl);
    for (auto *wire : wires) { wire->begin(); wire->setClock(100000); }
    // Initialize every CS high before clocks are driven on a shared bus.
    for (auto pins : superkeyboard::slots) {
        digitalWrite(pins.cs, HIGH); pinMode(pins.cs, OUTPUT);
    }
    digitalWrite(superkeyboard::primary_cs, HIGH);
    pinMode(superkeyboard::primary_cs, OUTPUT);
    digitalWrite(superkeyboard::secondary_cs, HIGH);
    pinMode(superkeyboard::secondary_cs, OUTPUT);
    SPI.setMOSI(superkeyboard::primary_mosi); SPI.setMISO(superkeyboard::primary_miso);
    SPI.setSCK(superkeyboard::primary_sck); SPI.begin();
    SPI1.setMOSI(superkeyboard::shared_mosi); SPI1.setMISO(superkeyboard::shared_miso);
    SPI1.setSCK(superkeyboard::shared_sck); SPI1.begin();
    return ESP_OK;
}
esp_err_t sk_i2c_transfer(unsigned bus, uint8_t address,
    const uint8_t *tx, size_t tx_length, uint8_t *rx, size_t rx_length) {
    if (bus >= 3 || address < 8 || address > 0x77 ||
        (tx_length && !tx) || (rx_length && !rx) || tx_length > 32 || rx_length > 32)
        return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(i2c_mutex[bus], pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    auto &wire = *wires[bus];
    esp_err_t result = ESP_OK;
    if (tx_length || !rx_length) {
        wire.beginTransmission(address);
        if (tx_length) wire.write(tx, tx_length);
        uint8_t error = wire.endTransmission(rx_length == 0);
        if (error) result = error == 2 ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    if (result == ESP_OK && rx_length) {
        const size_t count = wire.requestFrom(address, uint8_t(rx_length), uint8_t(true));
        for (size_t i = 0; i < count; ++i) rx[i] = wire.read();
        if (count != rx_length) result = ESP_FAIL;
    }
    xSemaphoreGive(i2c_mutex[bus]);
    return result;
}
esp_err_t sk_slot_claim(unsigned slot, const char *owner) {
    if (slot >= 3 || !owner || !*owner || strlen(owner) >= sizeof(owners[0])) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(slot_mutex, portMAX_DELAY);
    const bool available = !owners[slot][0] || owns(slot, owner);
    if (available) strlcpy(owners[slot], owner, sizeof(owners[slot]));
    xSemaphoreGive(slot_mutex);
    return available ? ESP_OK : ESP_ERR_INVALID_STATE;
}
esp_err_t sk_slot_release(unsigned slot, const char *owner) {
    xSemaphoreTake(slot_mutex, portMAX_DELAY);
    const bool allowed = owns(slot, owner);
    if (allowed) {
        if (uart_active[slot]) uarts[slot]->end();
        uart_active[slot] = false;
        digitalWrite(superkeyboard::slots[slot].cs, HIGH);
        owners[slot][0] = 0;
    }
    xSemaphoreGive(slot_mutex);
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}
esp_err_t sk_slot_spi(unsigned slot, const char *owner, uint32_t hz, uint8_t mode,
    const uint8_t *tx, uint8_t *rx, size_t length) {
    if (slot >= 3 || mode > 3 || !hz || hz > 12000000 || length > 4096 || (!tx && !rx))
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(slot_mutex, portMAX_DELAY);
    if (!owns(slot, owner)) { xSemaphoreGive(slot_mutex); return ESP_ERR_INVALID_STATE; }
    const auto pins = superkeyboard::slots[slot];
    if (xSemaphoreTake(spi_mutex[pins.spi], pdMS_TO_TICKS(100)) != pdTRUE) {
        xSemaphoreGive(slot_mutex); return ESP_ERR_TIMEOUT;
    }
    auto &spi = *spis[pins.spi];
    static const uint8_t modes[] = {SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3};
    spi.beginTransaction(SPISettings(hz, MSBFIRST, modes[mode]));
    digitalWrite(pins.cs, LOW);
    for (size_t i = 0; i < length; ++i) {
        const uint8_t value = spi.transfer(tx ? tx[i] : 0xff);
        if (rx) rx[i] = value;
    }
    digitalWrite(pins.cs, HIGH);
    spi.endTransaction();
    xSemaphoreGive(spi_mutex[pins.spi]);
    xSemaphoreGive(slot_mutex);
    return ESP_OK;
}
esp_err_t sk_slot_uart_begin(unsigned slot, const char *owner, uint32_t baud) {
    if (!baud || baud > 1000000) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(slot_mutex, portMAX_DELAY);
    const bool allowed = owns(slot, owner);
    if (allowed) { uarts[slot]->begin(baud); uart_active[slot] = true; }
    xSemaphoreGive(slot_mutex);
    return allowed ? ESP_OK : ESP_ERR_INVALID_STATE;
}
int sk_slot_uart_read(unsigned slot, const char *owner) {
    xSemaphoreTake(slot_mutex, portMAX_DELAY);
    const int result = owns(slot, owner) && uart_active[slot] ? uarts[slot]->read() : -1;
    xSemaphoreGive(slot_mutex);
    return result;
}
esp_err_t sk_slot_uart_write(unsigned slot, const char *owner, const uint8_t *data, size_t length) {
    if ((!data && length) || length > 4096) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(slot_mutex, portMAX_DELAY);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (owns(slot, owner) && uart_active[slot])
        result = uarts[slot]->write(data, length) == length ? ESP_OK : ESP_FAIL;
    xSemaphoreGive(slot_mutex);
    return result;
}
void sk_slots_print() {
    xSemaphoreTake(slot_mutex, portMAX_DELAY);
    for (unsigned i = 0; i < 3; ++i) {
        auto pins = superkeyboard::slots[i];
        sk_console_printf("slot%u: SPI%u CS%u I2C%u Serial%u RX%u TX%u owner=%s\r\n",
            i, pins.spi, pins.cs, pins.i2c, pins.uart, pins.rx, pins.tx,
            owners[i][0] ? owners[i] : "free");
    }
    xSemaphoreGive(slot_mutex);
}

bool sk_spi_lock(unsigned bus) {
    return bus < 2 && xSemaphoreTake(spi_mutex[bus], pdMS_TO_TICKS(100)) == pdTRUE;
}
void sk_spi_unlock(unsigned bus) { if (bus < 2) xSemaphoreGive(spi_mutex[bus]); }
bool sk_i2c_lock(unsigned bus) {
    return bus < 3 && xSemaphoreTake(i2c_mutex[bus], pdMS_TO_TICKS(100)) == pdTRUE;
}
void sk_i2c_unlock(unsigned bus) { if (bus < 3) xSemaphoreGive(i2c_mutex[bus]); }
