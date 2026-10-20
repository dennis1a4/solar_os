#include "usb_host_power.h"
#include "power_path.h"
#if SK_USB_HOST_POWER
#include <arduino_freertos.h>
#include "board.h"
#include "power_shutdown.h"
#if SK_USB_STORAGE
#include "usb_storage.h"
#include "storage_lock.h"
#endif
extern "C" {
#include "solar_os.h"
#include "solar_os_shell_io.h"
}
#if SK_POWER_PATH
// GPIO5 now receives expander INT; never drive it as the old host EN pin.
void sk_usb_host_power_begin() {}
void sk_usb_host_power_off() {sk_power_path_usb_off();}
extern "C" void sk_shell_cmd_usbpower(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);io->command_status=1;
    const char *op=argc==1?"status":argc==2?argv[1]:"";
    if(strcmp(op,"on") && strcmp(op,"off") && strcmp(op,"clear") && strcmp(op,"status")) {
        solar_os_shell_io_writeln(io,"usbpower status|on|off|clear");return;
    }
#if SK_USB_STORAGE
    if(!strcmp(op,"off")) {
        StorageLock lock;
        if(sk_usb_storage_mounted()) {solar_os_shell_io_writeln(io,"Unmount USB storage before turning host power off");return;}
        io->command_status=sk_power_path_usb(op)?0:1;
    } else
#endif
    io->command_status=sk_power_path_usb(op)?0:1;
    SkPowerPathSnapshot s{};sk_power_path_snapshot(&s);
    if(!s.expander_connected)solar_os_shell_io_writeln(io,"USB host: unconnected (MCP23008)");
    else solar_os_shell_io_printf(io,"USB host MCP23008 GP3 EN=%u GP4 FAULT=%u latched=%u GP5 SINK=%u (not measured VBUS)\n",s.usb_enabled,s.usb_fault,s.usb_latched,s.usb_attached);
    if(io->command_status)solar_os_shell_io_writeln(io,"Request failed; inactive faults need explicit clear before enable; shutdown blocks enable.");
}
#else
namespace {
volatile bool enabled=false, fault_latched=false;
bool initialized=false;
struct IrqGuard {
    uint32_t previous;
    IrqGuard():previous(__get_PRIMASK()) {__disable_irq();}
    ~IrqGuard(){if(!previous)__enable_irq();}
};
void fault_isr() {
    digitalWrite(superkeyboard::usb_host_enable,LOW);
    enabled=false; fault_latched=true;
}
bool fault_active() {return digitalRead(superkeyboard::usb_host_fault)==LOW;}
}
void sk_usb_host_power_begin() {
    IrqGuard guard;
    if(initialized) return;
    digitalWrite(superkeyboard::usb_host_enable,LOW);
    pinMode(superkeyboard::usb_host_enable,OUTPUT);
    // Hardware FAULT_N pull-up must connect to 3.3 V, not the USB 5 V rail.
    pinMode(superkeyboard::usb_host_fault,INPUT_PULLUP);
    enabled=false;
    attachInterrupt(superkeyboard::usb_host_fault,fault_isr,FALLING);
    if(fault_active()) fault_isr();
    initialized=true;
}
void sk_usb_host_power_off() {
    IrqGuard guard;
    if(initialized) digitalWrite(superkeyboard::usb_host_enable,LOW);
    enabled=false;
}
extern "C" void sk_shell_cmd_usbpower(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);io->command_status=1;
    if(argc>2) goto usage;
    {
        const char *op=argc==1?"status":argv[1];
        if(!initialized) {solar_os_shell_io_writeln(io,"USB power control not initialized");return;}
#if SK_USB_STORAGE
        if(!strcmp(op,"off")) {
            StorageLock lock;
            if(sk_usb_storage_mounted()) {solar_os_shell_io_writeln(io,"Unmount USB storage before turning host power off");return;}
            sk_usb_host_power_off();
        }
#endif
        bool ok=true, on, latched, active;
        {
            IrqGuard guard;
            if(fault_active()) fault_isr();
            if(!strcmp(op,"on")) {
                if(sk_power_requested() || fault_latched || fault_active()) ok=false;
                else {digitalWrite(superkeyboard::usb_host_enable,HIGH);enabled=true; if(fault_active()){fault_isr();ok=false;}}
            } else if(!strcmp(op,"off")) {digitalWrite(superkeyboard::usb_host_enable,LOW);enabled=false;}
            else if(!strcmp(op,"clear")) {if(fault_active())ok=false;else fault_latched=false;}
            else if(strcmp(op,"status")) goto usage;
            on=enabled;latched=fault_latched;active=fault_active();
        }
        solar_os_shell_io_printf(io,"USB host EN=%u FAULT_N=%u: enabled=%u fault=%u latched=%u (commanded state, not measured VBUS)\n",
            superkeyboard::usb_host_enable,superkeyboard::usb_host_fault,on,active,latched);
        if(!ok)solar_os_shell_io_writeln(io,"USB power request refused; clear an inactive fault before explicit enable");
        io->command_status=ok?0:1;return;
    }
usage:
    solar_os_shell_io_writeln(io,"usbpower status|on|off|clear (off at boot; clear never enables power)");
}
#endif
#endif
