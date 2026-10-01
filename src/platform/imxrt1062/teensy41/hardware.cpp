#if SK_HW_RESOURCES
#include <arduino_freertos.h>
#include "board.h"
#include "platform.h"
extern "C" {
#include "solar_os_resources.h"
}
static esp_err_t reserve(unsigned pin,const char *owner,const char *label) {
    return solar_os_resource_claim(SOLAR_OS_RESOURCE_GPIO_PIN,pin,-1,owner,label);
}
esp_err_t sk_resources_begin() {
    esp_err_t e=solar_os_resources_init();if(e!=ESP_OK)return e;
    struct Fixed {unsigned pin;const char *owner,*label;};
    using namespace superkeyboard;
    const Fixed fixed[]={
        {2,"board","shift clock"},{3,"board","NES latch"},{4,"board","NES data"},
        {5,"board","shift latch"},{6,"board","shift data"},
        {motor_enable,"board","motor enable"},{relay,"board","relay"},
        {amplifier_shutdown,"board","amplifier enable"},{vin_sense,"board","VIN sense"},{volume,"board","volume"},
        {primary_mosi,"spi0","MOSI"},{primary_miso,"spi0","MISO"},{primary_sck,"spi0","SCK"},
        {shared_mosi,"spi1","MOSI"},{shared_miso,"spi1","MISO"},{shared_sck,"spi1","SCK"},
        {audio_sda,"i2c0","SDA"},{audio_scl,"i2c0","SCL"},
        {wire1_sda,"i2c1","SDA"},{wire1_scl,"i2c1","SCL"},
        {wire2_sda,"i2c2","SDA"},{wire2_scl,"i2c2","SCL"},
#if SK_UART_CONSOLE
        {console_rx,"console","Serial1 RX"},{console_tx,"console","Serial1 TX"},
#endif
#if SK_PRIMARY_RA8875
        {primary_cs,"primary-display","CS"},{primary_reset,"primary-display","reset"},
        {primary_wait,"primary-display","WAIT"},
#endif
        // Preserve the board connector's dedicated secondary-panel/control pins,
        // even on builds where its optional panel driver is disabled.
        {secondary_cs,"board","secondary CS"},{secondary_reset,"board","secondary reset"},
        {secondary_dc,"board","secondary DC"},{secondary_backlight,"board","secondary backlight"},
#if SK_AUDIO_SGTL5000
        {audio_out,"audio","I2S TX"},{audio_in,"audio","I2S RX"},
        {audio_lrclk,"audio","I2S LRCLK"},{audio_bclk,"audio","I2S BCLK"},{audio_mclk,"audio","I2S MCLK"},
#endif
#if SK_SCOPE && SK_SCOPE_ADC_PIN >= 0
        {SK_SCOPE_ADC_PIN,"scope","ADC"},
#endif
    };
    for(auto &f:fixed)if((e=reserve(f.pin,f.owner,f.label))!=ESP_OK)return e;
    // Internal SDIO and QSPI pads are never exposed by the GPIO CLI (0..41).
    for(unsigned pin=42;pin<=47;++pin)if((e=reserve(pin,"sdio","SD card"))!=ESP_OK)return e;
    for(unsigned pin=48;pin<=54;++pin)if((e=reserve(pin,"qspi","flash/PSRAM"))!=ESP_OK)return e;
#if SK_AUDIO_SGTL5000
    e=solar_os_resource_claim(SOLAR_OS_RESOURCE_I2C_ADDRESS,0,0x0a,"audio","SGTL5000");
#endif
    return e;
}
#endif
