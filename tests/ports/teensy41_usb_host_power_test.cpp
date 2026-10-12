#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <vector>
#define SK_USB_HOST_POWER 1
#define SK_HW_RESOURCES 1
#define SK_LCD_CONSOLE 1
#include "../../src/platform/imxrt1062/teensy41/usb_host_power.cpp"
uint32_t test_clock,test_primask;
static int pins[42],modes[42];
static bool shutdown_pending;
extern "C" bool sk_power_requested(){return shutdown_pending;}
void digitalWrite(int pin,int value){pins[pin]=value;}
int digitalRead(int pin){return pins[pin];}
void pinMode(int pin,int mode){modes[pin]=mode;}
extern "C" void solar_os_shell_io_writeln(solar_os_shell_io_t *,const char *){}
extern "C" void solar_os_shell_io_printf(solar_os_shell_io_t *,const char *,...){}
static bool command(const char *op) {
    solar_os_shell_io_t io{};solar_os_context_t ctx{};ctx.io=&io;
    char name[]="usbpower";char *args[]={name,const_cast<char *>(op)};
    sk_shell_cmd_usbpower(&ctx,2,args);return io.command_status==0;
}
int main() {
    pins[6]=HIGH;sk_usb_host_power_begin();
    assert(pins[5]==LOW && modes[5]==OUTPUT && modes[6]==INPUT_PULLUP && !enabled);
    shutdown_pending=true;assert(!command("on") && pins[5]==LOW);shutdown_pending=false;
    assert(command("on") && pins[5]==HIGH);
    fault_isr();assert(pins[5]==LOW && !enabled && fault_latched);
    assert(!command("on")); // Short pulse is still latched with input now high.
    pins[6]=LOW;assert(!command("clear"));assert(!command("on"));
    pins[6]=HIGH;assert(command("clear") && pins[5]==LOW);
    assert(command("on") && pins[5]==HIGH);
    assert(command("off") && pins[5]==LOW);
    assert(command("on"));sk_usb_host_power_off();assert(!enabled && pins[5]==LOW);
    test_primask=1;assert(command("status"));assert(test_primask==1);
    test_primask=0;assert(command("status"));assert(!test_primask);
    initialized=false;pins[6]=LOW;fault_latched=false;sk_usb_host_power_begin();
    assert(fault_latched && pins[5]==LOW && !enabled);
    puts("PASS: EN5/FAULT6, boot-off, pulse latch, clear/enable separation, shutdown and interrupt state");
}
