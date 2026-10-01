#include <arduino_freertos.h>
#include <Wire.h>
#include <SPI.h>
#include <semphr.h>
#include "board.h"
#include "platform.h"
#if SK_HW_RESOURCES
extern "C" {
#include "solar_os_resources.h"
#include "solar_os_buses.h"
#include "solar_os_uart.h"
}
#endif

static SemaphoreHandle_t i2c_mutex[3], spi_mutex[2], slot_mutex;
static TwoWire *const wires[] = {&Wire, &Wire1, &Wire2};
static SPIClass *const spis[] = {&SPI, &SPI1};
static HardwareSerialIMXRT *const uarts[] = {&Serial7, &Serial8, &Serial3};
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
#if SK_HW_RESOURCES
    esp_err_t resources=sk_resources_begin();if(resources!=ESP_OK)return resources;
#endif
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
    bool available = !owners[slot][0] || owns(slot, owner);
#if SK_HW_RESOURCES
    if(available)available=solar_os_resource_claim(SOLAR_OS_RESOURCE_GPIO_PIN,
        superkeyboard::slots[slot].cs,-1,owner,"slot CS")==ESP_OK;
#endif
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
#if SK_HW_RESOURCES
        solar_os_resource_release(SOLAR_OS_RESOURCE_GPIO_PIN,superkeyboard::slots[slot].cs,-1,owner);
#endif
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
#if SK_HW_RESOURCES
esp_err_t sk_slot_uart_begin(unsigned slot,const char *owner,uint32_t baud) {return sk_uart_claim(slot,owner,baud);}
int sk_slot_uart_read(unsigned slot,const char *owner) {
    if(slot>=3)return -1;
    char name[16];snprintf(name,sizeof(name),"uart%u",superkeyboard::slots[slot].uart);
    solar_os_uart_status_t state{};if(!owner || !solar_os_uart_get_bus_status(name,&state) || strcmp(state.port_owner,owner))return -1;
    uint8_t data;size_t n=0;return solar_os_bus_uart_read(name,&data,1,0,&n)==ESP_OK && n?data:-1;
}
esp_err_t sk_slot_uart_write(unsigned slot,const char *owner,const uint8_t *data,size_t len) {
    if(slot>=3 || !owner)return ESP_ERR_INVALID_ARG;
    char name[16];snprintf(name,sizeof(name),"uart%u",superkeyboard::slots[slot].uart);
    solar_os_uart_status_t state{};if(!solar_os_uart_get_bus_status(name,&state) || strcmp(state.port_owner,owner))return ESP_ERR_INVALID_STATE;
    size_t n=0;return solar_os_bus_uart_write(name,data,len,&n);
}
#else
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
#endif

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
#if SK_HW_RESOURCES
// UART IRQs write into internal OCRAM, never the PSRAM heap. This retains a
// bounded burst between COM tick events; no lossless/flow-control claim is made.
DMAMEM static uint8_t uart_rx_extra[3][4096];
static char uart_owners[3][SOLAR_OS_RESOURCE_OWNER_MAX];
static uint32_t uart_baud[3]={115200,115200,115200};
static const char *uart_names[]={"uart7","uart8","uart3"};
static int uart_index(const char *name) {
    for(unsigned i=0;i<3;++i)if(name && !strcmp(name,uart_names[i]))return i;
    return -1;
}
const char *sk_slot_owner(unsigned slot) {return slot<3?owners[slot]:"invalid";}
esp_err_t sk_uart_claim(unsigned slot,const char *owner,uint32_t baud) {
    if(slot>=3 || !owner || !*owner || strlen(owner)>=sizeof(uart_owners[0]) || baud<300 || baud>1000000)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(slot_mutex,portMAX_DELAY);
    if(uart_owners[slot][0]) {xSemaphoreGive(slot_mutex);return ESP_ERR_INVALID_STATE;}
    auto p=superkeyboard::slots[slot];
    const solar_os_resource_request_t req[]={
        {SOLAR_OS_RESOURCE_GPIO_PIN,p.rx,-1,"UART RX"},
        {SOLAR_OS_RESOURCE_GPIO_PIN,p.tx,-1,"UART TX"},
        {SOLAR_OS_RESOURCE_UART_PORT,p.uart,-1,"UART"}};
    esp_err_t e=solar_os_resource_claim_bundle(req,3,owner,nullptr);
    if(e==ESP_OK) {strlcpy(uart_owners[slot],owner,sizeof(uart_owners[0]));uart_baud[slot]=baud;uarts[slot]->addMemoryForRead(uart_rx_extra[slot],sizeof(uart_rx_extra[slot]));uarts[slot]->begin(baud);}
    xSemaphoreGive(slot_mutex);return e;
}
esp_err_t sk_uart_release(unsigned slot,const char *owner) {
    if(slot>=3 || !owner)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(slot_mutex,portMAX_DELAY);
    esp_err_t e=ESP_ERR_INVALID_STATE;
    if(uart_owners[slot][0] && !strcmp(uart_owners[slot],owner)) {
        uarts[slot]->end();auto p=superkeyboard::slots[slot];
        pinMode(p.rx,INPUT);pinMode(p.tx,INPUT);
        solar_os_resource_release(SOLAR_OS_RESOURCE_GPIO_PIN,p.rx,-1,owner);
        solar_os_resource_release(SOLAR_OS_RESOURCE_GPIO_PIN,p.tx,-1,owner);
        solar_os_resource_release(SOLAR_OS_RESOURCE_UART_PORT,p.uart,-1,owner);
        uart_owners[slot][0]=0;e=ESP_OK;
    }
    xSemaphoreGive(slot_mutex);return e;
}
extern "C" bool solar_os_bus_find(const char *name,solar_os_bus_protocol_t protocol,solar_os_bus_info_t *info) {
    int i=uart_index(name);if(i<0 || protocol!=SOLAR_OS_BUS_PROTOCOL_UART || !info)return false;
    xSemaphoreTake(slot_mutex,portMAX_DELAY);memset(info,0,sizeof(*info));
    auto p=superkeyboard::slots[i];info->active=info->attached=info->ready=true;info->id=i;
    info->protocol=protocol;info->origin=SOLAR_OS_BUS_ORIGIN_BOARD;info->sharing=SOLAR_OS_BUS_EXCLUSIVE;
    info->lease_count=uart_owners[i][0]?1:0;strlcpy(info->name,name,sizeof(info->name));
    info->config.uart={p.uart,p.tx,p.rx,uart_baud[i]};
    xSemaphoreGive(slot_mutex);return true;
}
extern "C" esp_err_t solar_os_bus_acquire(const char *name,solar_os_bus_protocol_t protocol,const char *owner) {
    int i=uart_index(name);return i<0 || protocol!=SOLAR_OS_BUS_PROTOCOL_UART?ESP_ERR_NOT_FOUND:sk_uart_claim(i,owner,uart_baud[i]);
}
extern "C" esp_err_t solar_os_bus_release(const char *name,solar_os_bus_protocol_t protocol,const char *owner) {
    int i=uart_index(name);return i<0 || protocol!=SOLAR_OS_BUS_PROTOCOL_UART?ESP_ERR_NOT_FOUND:sk_uart_release(i,owner);
}
extern "C" esp_err_t solar_os_bus_uart_write(const char *name,const uint8_t *data,size_t len,size_t *written) {
    if(written)*written=0;int i=uart_index(name);
    if(i<0 || !written || (!data && len) || len>128)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(slot_mutex,portMAX_DELAY);esp_err_t e=ESP_ERR_INVALID_STATE;
    if(uart_owners[i][0]) {
        size_t n=std::min(len,size_t(uarts[i]->availableForWrite()));
        *written=n?uarts[i]->write(data,n):0;e=*written==len?ESP_OK:ESP_ERR_TIMEOUT;
    }
    xSemaphoreGive(slot_mutex);return e;
}
extern "C" esp_err_t solar_os_bus_uart_read(const char *name,uint8_t *data,size_t len,uint32_t timeout,size_t *read_len) {
    if(read_len)*read_len=0;int i=uart_index(name);
    if(i<0 || !read_len || (!data && len) || len>128 || timeout)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(slot_mutex,portMAX_DELAY);esp_err_t e=ESP_ERR_INVALID_STATE;
    if(uart_owners[i][0]) {while(*read_len<len && uarts[i]->available())data[(*read_len)++]=uarts[i]->read();e=ESP_OK;}
    xSemaphoreGive(slot_mutex);return e;
}
extern "C" bool solar_os_uart_get_bus_status(const char *name,solar_os_uart_status_t *out) {
    int i=uart_index(name);if(i<0 || !out)return false;
    xSemaphoreTake(slot_mutex,portMAX_DELAY);memset(out,0,sizeof(*out));auto p=superkeyboard::slots[i];
    strlcpy(out->name,name,sizeof(out->name));out->attached=true;out->initialized=uart_owners[i][0];
    out->port_num=p.uart;out->tx_pin=p.tx;out->rx_pin=p.rx;out->baud_rate=uart_baud[i];out->mode=SOLAR_OS_UART_MODE_RAW;
    out->rx_buffered_valid=true;out->rx_buffered=out->initialized?uarts[i]->available():0;
    out->port_claimed=uart_owners[i][0];strlcpy(out->port_owner,uart_owners[i],sizeof(out->port_owner));
    xSemaphoreGive(slot_mutex);return true;
}
extern "C" const char *solar_os_uart_mode_name(solar_os_uart_mode_t) {return "8N1 raw";}
extern "C" esp_err_t solar_os_bus_uart_autobaud_start(const char *,const char *) {return ESP_ERR_NOT_SUPPORTED;}
extern "C" esp_err_t solar_os_bus_uart_autobaud_finish(const char *,const char *,solar_os_bus_uart_autobaud_result_t *) {return ESP_ERR_NOT_SUPPORTED;}
extern "C" esp_err_t solar_os_bus_uart_autobaud_cancel(const char *,const char *) {return ESP_ERR_NOT_SUPPORTED;}
#endif
