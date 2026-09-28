#if SK_PLOT
#include <arduino_freertos.h>
extern "C" {
#include "solar_os_stream.h"
#include "solar_os_time.h"
}
extern "C" uint64_t solar_os_time_uptime_ms() { return uint64_t(xTaskGetTickCount())*portTICK_PERIOD_MS; }
extern "C" esp_err_t solar_os_time_get_utc_epoch_ms(uint64_t *out) {
    if(!out)return ESP_ERR_INVALID_ARG;
    const uint32_t seconds=Teensy3Clock.get();
    if(seconds<1577836800)return ESP_ERR_INVALID_STATE;
    *out=uint64_t(seconds)*1000;return ESP_OK;
}
static esp_err_t read_uptime(void *,const solar_os_stream_read_options_t *,float *out) {
    *out=solar_os_time_uptime_ms()/1000.0f;return ESP_OK;
}
void sk_plot_streams_begin() {
    solar_os_stream_driver_t driver{};
    strlcpy(driver.info.id,"uptime",sizeof(driver.info.id));
    strlcpy(driver.info.provider,"teensy41",sizeof(driver.info.provider));
    strlcpy(driver.info.format,"float",sizeof(driver.info.format));
    driver.info.type=SOLAR_OS_STREAM_TYPE_SCALAR;
    driver.info.direction=SOLAR_OS_STREAM_DIRECTION_SOURCE;
    driver.info.sharing=SOLAR_OS_STREAM_SHARING_SHARED;
    strlcpy(driver.info.unit,"s",sizeof(driver.info.unit));
    strlcpy(driver.info.summary,"Teensy uptime in seconds",sizeof(driver.info.summary));
    driver.read_scalar=read_uptime;
    configASSERT(solar_os_stream_register(&driver)==ESP_OK);
}
#endif
