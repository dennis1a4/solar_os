#include <arduino_freertos.h>
#include <Wire.h>
#include <SPI.h>
#include "board.h"
#include "platform.h"
extern "C" {
#include "solar_os_resources.h"
#include "solar_os_buses.h"
#include "solar_os_uart.h"
}
int test_modes[64],test_values[64],test_pin_calls[64];
HardwareSerial Serial7,Serial8,Serial3;TwoWire Wire,Wire1,Wire2;SPIClass SPI,SPI1;
extern "C" size_t strlcpy(char *out,const char *in,size_t size){size_t len=strlen(in);if(size){size_t n=std::min(len,size-1);memcpy(out,in,n);out[n]=0;}return len;}
void sk_console_printf(const char *,...){}
static bool claimed(int pin,const char *owner) {
    solar_os_resource_claim_t c{};
    return solar_os_resource_find_claim(SOLAR_OS_RESOURCE_GPIO_PIN,pin,-1,&c) && !strcmp(owner,c.owner);
}
int main() {
    assert(sk_buses_begin()==ESP_OK);
    assert(sk_slot_claim(2,"primary-display")==ESP_OK);
    const size_t baseline=solar_os_resource_claim_count();
#if SK_PRIMARY_CS == 10
    assert(claimed(10,"primary-display") && claimed(14,"primary-display") && claimed(15,"primary-display"));
    assert(!claimed(37,"primary-display") && !claimed(10,"board"));
    assert(superkeyboard::primary_wait==-1 && superkeyboard::primary_backlight==15);
    assert(superkeyboard::secondary_spi==0 && superkeyboard::secondary_mosi==11 && superkeyboard::secondary_sck==13);
    assert(claimed(16,"i2c1") && claimed(17,"i2c1"));
    for(unsigned pin=30;pin<=33;++pin)assert(claimed(pin,"secondary-display"));
    assert(sk_slot_claim(0,"hw:usb")==ESP_OK);
    assert(sk_slot_release(0,"hw:usb")==ESP_OK);
#else
    assert(claimed(37,"primary-display") && claimed(9,"primary-display") && claimed(15,"primary-display"));
#endif
    assert(claimed(0,"console") && claimed(40,"board") && claimed(13,"spi0") && claimed(49,"qspi"));
    assert(sk_uart_claim_format(1,"format-test",9600,0x102)==ESP_OK);
    assert(Serial8.format==0x102);
    assert(sk_uart_release(1,"format-test")==ESP_OK);
    assert(sk_uart_claim(1,"format-test",115200)==ESP_OK && Serial8.format==0);
    assert(sk_uart_release(1,"format-test")==ESP_OK);
    assert(sk_uart_claim_format(1,"format-test",9600,0x200)==ESP_ERR_INVALID_ARG);
    // GPIO reservation collisions must reject before any UART pin or driver changes.
    assert(solar_os_resource_claim(SOLAR_OS_RESOURCE_GPIO_PIN,28,-1,"gpio:usb","GPIO")==ESP_OK);
    assert(sk_uart_claim(0,"hw:usb",115200)==ESP_ERR_INVALID_STATE);assert(Serial7.begins==0);
    assert(!claimed(29,"hw:usb"));assert(solar_os_resource_claim_count()==baseline+1);
    solar_os_resource_release_owner("gpio:usb");
    // UART3's second fixed pin must roll back its entire bundle.
    assert(sk_uart_claim(2,"app-com",115200)==ESP_ERR_INVALID_STATE);assert(Serial3.begins==0);
    assert(solar_os_resource_claim_count()==baseline);
#if SK_PRIMARY_CS != 10
    assert(sk_slot_claim(0,"hw:usb")==ESP_ERR_INVALID_STATE);
#endif
    assert(sk_slot_claim(2,"hw:usb")==ESP_ERR_INVALID_STATE);
    for(unsigned cycle=0;cycle<20;++cycle) {
        assert(sk_uart_claim(0,"app-com",115200)==ESP_OK);
        assert(sk_uart_claim(0,"hw:lcd",115200)==ESP_ERR_INVALID_STATE);
        assert(sk_uart_release(0,"hw:lcd")==ESP_ERR_INVALID_STATE);
        assert(Serial7.active && claimed(28,"app-com") && claimed(29,"app-com"));
        assert(solar_os_resource_claim(SOLAR_OS_RESOURCE_GPIO_PIN,29,-1,"gpio:usb","GPIO")==ESP_ERR_INVALID_STATE);
        uint8_t bytes[32]={};size_t n=0;
        assert(solar_os_bus_uart_write("uart7",bytes,32,&n)==ESP_ERR_TIMEOUT && n==16);
        Serial7.room=0;assert(solar_os_bus_uart_write("uart7",bytes,1,&n)==ESP_ERR_TIMEOUT && n==0);Serial7.room=16;
        assert(solar_os_bus_uart_read("uart7",bytes,32,0,&n)==ESP_OK && n==0);
        assert(sk_uart_release(0,"app-com")==ESP_OK && !Serial7.active);
        assert(solar_os_resource_claim_count()==baseline && test_modes[28]==INPUT && test_modes[29]==INPUT);
        assert(sk_slot_claim(1,"hw:usb")==ESP_OK && claimed(36,"hw:usb"));
        auto before=SPI1.transfers;
        assert(sk_slot_spi(1,"hw:lcd",1000000,0,bytes,bytes,1)==ESP_ERR_INVALID_STATE && SPI1.transfers==before);
        assert(sk_slot_spi(1,"hw:usb",1000000,0,bytes,bytes,1)==ESP_OK && SPI1.transfers==before+1);
        assert(sk_slot_release(1,"hw:lcd")==ESP_ERR_INVALID_STATE);
        assert(sk_slot_release(1,"hw:usb")==ESP_OK && test_values[36]==HIGH);
        assert(solar_os_resource_claim_count()==baseline);
    }
    // Invalid UART inputs do not start a peripheral or allocate claims.
    assert(sk_uart_claim(3,"x",115200)==ESP_ERR_INVALID_ARG);
    assert(sk_uart_claim(0,"x",0)==ESP_ERR_INVALID_ARG);
    assert(sk_uart_release(99,"x")==ESP_ERR_INVALID_ARG);
    assert(solar_os_resource_claim_count()==baseline);
    puts("PASS: fixed pin reservations, atomic UART rollback, GPIO/UART exclusion, cross-owner denial, bounded UART writes, SPI CS ownership, 20 release cycles");
}
