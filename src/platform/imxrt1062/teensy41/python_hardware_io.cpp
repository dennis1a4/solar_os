#if SK_HW_RESOURCES
#include <arduino_freertos.h>
#include "platform.h"
#include "board.h"
#include "python_hardware_io.h"
extern "C" {
#include "solar_os_resources.h"
#include "solar_os_buses.h"
bool sk_python_poll_cancel(void);
void sk_console_delay_ms(uint32_t);
}
struct Handle {uint32_t token; int kind,id,option,value; uint8_t addresses[16];};
static Handle handles[16];
static uint32_t sequence;
static void owner(uint32_t token,char *out) {snprintf(out,24,"python.hw.%lu",(unsigned long)token);}
static Handle *find(uint32_t token) {for(auto &h:handles)if(token && h.token==token)return &h;return nullptr;}
static const char *uart_names[]={"uart7","uart8","uart3"};
extern int sk_uart_available(unsigned,const char *);
extern "C" esp_err_t sk_py_hw_open(int kind,int id,int option,int value,uint32_t *token) {
    Handle *h=nullptr;for(auto &item:handles)if(!item.token){h=&item;break;}
    if(!h || sequence==UINT32_MAX)return ESP_ERR_NO_MEM;
    if(kind<0 || kind>5 || id<0)return ESP_ERR_INVALID_ARG;
    if(kind<=2 && id>41)return ESP_ERR_INVALID_ARG;
    if(kind==0 && (option<0 || option>3 || value<0 || value>1))return ESP_ERR_INVALID_ARG;
#if SK_SCOPE && SK_SCOPE_ADC_PIN >= 0
    // The scope DMA backend configures ADC0 globally. Until controller-level
    // arbitration exists, do not offer independent Python conversions there.
    if(kind==1)return ESP_ERR_NOT_SUPPORTED;
#endif
    if(kind==1 && !((id>=14 && id<=27)||(id>=38 && id<=41)))return ESP_ERR_NOT_SUPPORTED;
    // These two PWM groups have no fixed board function. Claim both channels,
    // because changing frequency also changes the partner pin's timer.
    if(kind==2 && (id!=28 && id!=29 && id!=36 && id!=37))return ESP_ERR_NOT_SUPPORTED;
    if(kind==2 && (option<1 || option>100000 || value<0 || value>65535))return ESP_ERR_INVALID_ARG;
    if(kind>=3 && id>2)return ESP_ERR_INVALID_ARG;
    if(kind==3 && option!=100000)return ESP_ERR_NOT_SUPPORTED;
    if(kind==4 && (option<1 || option>12000000 || value<0 || value>3))return ESP_ERR_INVALID_ARG;
    char who[24];uint32_t next=++sequence;owner(next,who);esp_err_t err=ESP_OK;
    if(kind<=2) {
        solar_os_resource_request_t pins[2]={{SOLAR_OS_RESOURCE_GPIO_PIN,id,-1,"Python pin"},{SOLAR_OS_RESOURCE_GPIO_PIN,id^1,-1,"Python PWM timer"}};
        err=solar_os_resource_claim_bundle(pins,kind==2?2:1,who,nullptr);
        if(err==ESP_OK && kind==0) {if(option==1)digitalWrite(id,value);pinMode(id,option==0?INPUT:option==1?OUTPUT:option==2?INPUT_PULLUP:INPUT_PULLDOWN);}
        if(err==ESP_OK && kind==1)pinMode(id,INPUT);
        if(err==ESP_OK && kind==2) {analogWriteFrequency(id,option);uint32_t old=analogWriteResolution(16);analogWrite(id,value);analogWriteResolution(old);}
    } else if(kind==4)err=sk_slot_claim(id,who);
    else if(kind==5)err=sk_uart_claim(id,who,option);
    if(err!=ESP_OK)return err;
    *h={next,kind,id,option,value,{}};*token=next;return ESP_OK;
}
extern "C" esp_err_t sk_py_hw_close(uint32_t token) {
    auto *h=find(token);if(!h)return ESP_ERR_INVALID_STATE;
    char who[24];owner(token,who);
    if(h->kind<=2) {if(h->kind==2)analogWrite(h->id,0);pinMode(h->id,INPUT);}
    if(h->kind==4)sk_slot_release(h->id,who);
    if(h->kind==5)sk_uart_release(h->id,who);
    solar_os_resource_release_owner(who);memset(h,0,sizeof(*h));return ESP_OK;
}
extern "C" void sk_py_hw_reset(void) {for(auto &h:handles)if(h.token)sk_py_hw_close(h.token);}
extern "C" esp_err_t sk_py_hw_value(uint32_t token,int op,int value,int *result) {
    auto *h=find(token);if(!h)return ESP_ERR_INVALID_STATE;
    *result=0;
    if(h->kind==0) {if(op==1){if(h->option!=1 || value<0 || value>1)return ESP_ERR_INVALID_ARG;digitalWrite(h->id,value);}else if(op!=0)return ESP_ERR_INVALID_ARG;*result=digitalRead(h->id);}
    else if(h->kind==1 && op==0) { // Arduino ADC is kept at its default 10-bit resolution.
        *result=analogRead(h->id);*result=(*result<<6)|(*result>>4);
    } else if(h->kind==2) {
        if(op==1) {if(value<0 || value>65535)return ESP_ERR_INVALID_ARG;uint32_t old=analogWriteResolution(16);analogWrite(h->id,value);analogWriteResolution(old);h->value=value;}
        else if(op==2) {if(value<1 || value>100000)return ESP_ERR_INVALID_ARG;analogWriteFrequency(h->id,value);h->option=value;}
        else if(op!=0 && op!=3)return ESP_ERR_INVALID_ARG;
        *result=op==3?h->option:h->value;
    } else if(h->kind==5 && op==0) {char who[24];owner(token,who);*result=sk_uart_available(h->id,who);}
    else return ESP_ERR_NOT_SUPPORTED;
    return ESP_OK;
}
extern "C" esp_err_t sk_py_hw_transfer(uint32_t token,int address,const uint8_t *tx,size_t txlen,uint8_t *rx,size_t rxlen,size_t *received) {
    auto *h=find(token);if(!h)return ESP_ERR_INVALID_STATE;
    if(txlen>4096 || rxlen>4096)return ESP_ERR_INVALID_SIZE;
    char who[24];owner(token,who);*received=0;esp_err_t err;
    if(h->kind==3) {
        if(address<8 || address>0x77 || txlen>32 || rxlen>32)return ESP_ERR_INVALID_ARG;
        if(!(h->addresses[address/8]&(1U<<(address%8)))) {
            err=solar_os_resource_claim(SOLAR_OS_RESOURCE_I2C_ADDRESS,h->id,address,who,"Python I2C");
            if(err!=ESP_OK)return err;
            h->addresses[address/8]|=1U<<(address%8);
        }
        err=sk_i2c_transfer(h->id,address,tx,txlen,rx,rxlen);
        // An absent address does not need a persistent lease (scan support).
        if(err==ESP_ERR_NOT_FOUND) {solar_os_resource_release(SOLAR_OS_RESOURCE_I2C_ADDRESS,h->id,address,who);h->addresses[address/8]&=~(1U<<(address%8));}
        if(err==ESP_OK)*received=rxlen;return err;
    }
    if(h->kind==4) {if(txlen && rxlen && txlen!=rxlen)return ESP_ERR_INVALID_ARG;err=sk_slot_spi(h->id,who,h->option,h->value,tx,rx,txlen?txlen:rxlen);if(err==ESP_OK)*received=rxlen;return err;}
    if(h->kind==5) {
        if(txlen && rxlen)return ESP_ERR_INVALID_ARG;
        if(txlen)return solar_os_bus_uart_write(uart_names[h->id],tx,txlen,received);
        return solar_os_bus_uart_read(uart_names[h->id],rx,rxlen,0,received);
    }
    return ESP_ERR_NOT_SUPPORTED;
}
extern "C" uint32_t sk_py_ticks(int us) {return (us?micros():millis())&0x3fffffff;}
extern "C" void sk_py_delay(uint32_t ms) {sk_console_delay_ms(ms);}
#endif
