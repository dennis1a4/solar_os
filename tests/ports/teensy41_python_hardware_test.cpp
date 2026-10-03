#include <arduino_freertos.h>
#include <Wire.h>
#include <SPI.h>
#include "platform.h"
#include "python_hardware_io.h"
extern "C" {
#include "solar_os_resources.h"
}
int test_modes[64],test_values[64],test_pin_calls[64];
HardwareSerial Serial7,Serial8,Serial3;TwoWire Wire,Wire1,Wire2;SPIClass SPI,SPI1;
extern "C" size_t strlcpy(char *out,const char *in,size_t size){size_t n=strlen(in);if(size){size_t copy=std::min(n,size-1);memcpy(out,in,copy);out[copy]=0;}return n;}
void sk_console_printf(const char *,...){}
extern "C" void sk_console_delay_ms(uint32_t){}
int main(){
    assert(sk_buses_begin()==ESP_OK);size_t baseline=solar_os_resource_claim_count();
    uint32_t h=0,other=0;int value=0;size_t received;uint8_t tx[]={1,2,3},rx[33]={};
    assert(sk_py_hw_open(0,48,0,0,&h)==ESP_ERR_INVALID_ARG);
    assert(sk_py_hw_open(0,2,0,0,&h)==ESP_ERR_INVALID_STATE);
    assert(sk_py_hw_open(0,28,1,1,&h)==ESP_OK);
    assert(sk_py_hw_value(h,0,0,&value)==ESP_OK && value==1);
    assert(sk_py_hw_open(5,0,115200,0,&other)==ESP_ERR_INVALID_STATE);
    assert(sk_py_hw_close(h)==ESP_OK);
    assert(sk_py_hw_value(h,0,0,&value)==ESP_ERR_INVALID_STATE);
    assert(sk_py_hw_open(0,29,0,0,&other)==ESP_OK);
    assert(sk_py_hw_open(2,28,1000,0,&h)==ESP_ERR_INVALID_STATE);
    assert(solar_os_resource_claim_count()==baseline+1);
    sk_py_hw_close(other);
    assert(sk_py_hw_open(2,28,1000,123,&h)==ESP_OK);
    assert(sk_py_hw_value(h,1,65535,&value)==ESP_OK && value==65535);
    assert(sk_py_hw_value(h,1,65536,&value)==ESP_ERR_INVALID_ARG);
    sk_py_hw_close(h);assert(test_values[28]==0 && test_modes[28]==INPUT);
    assert(sk_py_hw_open(1,14,0,0,&h)==ESP_OK);
    assert(sk_py_hw_value(h,0,0,&value)==ESP_OK && value==32800);sk_py_hw_close(h);
    assert(sk_py_hw_open(3,0,100000,0,&h)==ESP_OK);
    assert(sk_py_hw_transfer(h,10,tx,1,nullptr,0,&received)==ESP_ERR_INVALID_STATE);
    assert(sk_py_hw_transfer(h,64,nullptr,0,rx,33,&received)==ESP_ERR_INVALID_ARG);
    assert(sk_py_hw_transfer(h,64,nullptr,0,nullptr,0,&received)==ESP_ERR_NOT_FOUND);
    assert(solar_os_resource_claim_count()==baseline);sk_py_hw_close(h);
    assert(sk_py_hw_open(4,1,1000000,0,&h)==ESP_OK);
    assert(sk_py_hw_transfer(h,0,tx,3,rx,3,&received)==ESP_OK && received==3 && !memcmp(tx,rx,3));
    assert(sk_py_hw_transfer(h,0,tx,4097,nullptr,0,&received)==ESP_ERR_INVALID_SIZE);
    sk_py_hw_close(h);
    assert(sk_py_hw_open(5,1,31250,0,&h)==ESP_OK);
    assert(sk_py_hw_transfer(h,0,tx,3,nullptr,0,&received)==ESP_OK && received==3);
    sk_py_hw_reset();assert(!Serial8.active && solar_os_resource_claim_count()==baseline);
    for(unsigned i=0;i<16;++i)assert(sk_py_hw_open(3,2,100000,0,&h)==ESP_OK);
    assert(sk_py_hw_open(3,2,100000,0,&other)==ESP_ERR_NO_MEM);
    sk_py_hw_reset();assert(solar_os_resource_claim_count()==baseline);
    puts("PASS: native Python hardware ownership, rollback, transfer limits, PWM cleanup, stale handles and exhaustion");
}
