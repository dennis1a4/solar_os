#if SK_MOTOR_MODULE
#include <arduino_freertos.h>
#include <semphr.h>
#include <stdlib.h>
#include "board.h"
#include "platform.h"
#include "motor_module.h"
extern "C" {
#include "solar_os.h"
#include "solar_os_shell_io.h"
#include "solar_os_resources.h"
bool sk_power_requested();
}
// Custom SK I2C protocol v1. Each command is exactly three bytes; a read
// returns 16 bytes. Validate identity, capabilities and sequence, not just ACK.
namespace {
constexpr uint8_t arm_op=0xe2, heartbeat_op=0xe3, stop_op=0xe4;
struct Motor {
    bool attached=false, moving=false, stepping=false, failed=false;
    uint8_t address=0x10, status[16]={};
    uint32_t deadline=0, heartbeat=0;
};
Motor motors[2];
const char *const owners[]={"motor:slot0","motor:slot1"};
StaticSemaphore_t mutex_storage;
SemaphoreHandle_t mutex;
TaskHandle_t worker;
DMAMEM StackType_t worker_stack[1024];
StaticTask_t worker_tcb;
void init_lock() {
    taskENTER_CRITICAL();
    if(!mutex) mutex=xSemaphoreCreateMutexStatic(&mutex_storage);
    taskEXIT_CRITICAL();
}
struct Guard { Guard(){init_lock();xSemaphoreTake(mutex,portMAX_DELAY);} ~Guard(){xSemaphoreGive(mutex);} };
bool read_status(unsigned slot) {
    auto &m=motors[slot]; uint8_t data[16];
    if(sk_i2c_transfer(superkeyboard::slots[slot].i2c,m.address,nullptr,0,data,16)!=ESP_OK)
        return false;
    if(data[0]!='S' || data[1]!='K' || data[2]!=1 || data[12]!=m.address || (data[13]&12)!=12)
        return false;
    memcpy(m.status,data,16); return true;
}
bool command(unsigned slot,uint8_t op,uint8_t a=0,uint8_t b=0) {
    auto &m=motors[slot];
    if(!read_status(slot)) return false;
    uint8_t sequence=m.status[6], data[]={op,a,b};
    // Never replay an ambiguous write (particularly a step-count command).
    if(sk_i2c_transfer(superkeyboard::slots[slot].i2c,m.address,data,3,nullptr,0)!=ESP_OK)
        return false;
    return read_status(slot) && m.status[6]==uint8_t(sequence+1) && m.status[5]==0;
}
bool stop(unsigned slot) {
    auto &m=motors[slot]; m.moving=false; m.stepping=false;
    bool ok=command(slot,stop_op) && !(m.status[4]&8);
    m.failed=!ok; return ok;
}
void fail(unsigned slot) {
    stop(slot); motors[slot].failed=true;
    // If stop could not be confirmed, cease heartbeats: AVR lease expires.
}
void poll() {
    Guard guard;
    for(unsigned slot=0;slot<2;slot++) {
        auto &m=motors[slot]; if(!m.attached || !m.moving) continue;
        uint32_t now=millis();
        if(sk_power_requested() || int32_t(now-m.deadline)>=0) { stop(slot); continue; }
        if(!read_status(slot) || !(m.status[4]&8) || m.status[5]!=0) { fail(slot); continue; }
        if(m.stepping && !m.status[11]) { stop(slot); continue; }
        if(uint32_t(now-m.heartbeat)>=500) {
            if(!command(slot,heartbeat_op,0xa5,0x5a)) fail(slot);
            else m.heartbeat=now;
        }
    }
}
void run(void *) {
    for(;;) {
        poll(); bool active=false;
        { Guard guard; for(auto &m:motors) active|=m.moving; }
        ulTaskNotifyTake(pdTRUE,active?pdMS_TO_TICKS(20):portMAX_DELAY);
    }
}
bool attach(unsigned slot,uint8_t address) {
    auto &m=motors[slot]; if(m.attached) return false;
    unsigned bus=superkeyboard::slots[slot].i2c;
    if(solar_os_resource_claim(SOLAR_OS_RESOURCE_I2C_ADDRESS,bus,address,owners[slot],"motor controller")!=ESP_OK)
        return false;
    m=Motor{}; m.address=address;
    // Attaching is allowed only to identified hardware, and always disarms it.
    if(!read_status(slot) || !stop(slot)) {
        solar_os_resource_release(SOLAR_OS_RESOURCE_I2C_ADDRESS,bus,address,owners[slot]); return false;
    }
    if(!worker) worker=xTaskCreateStatic(run,"motor",1024,nullptr,1,worker_stack,&worker_tcb);
    if(!worker) {solar_os_resource_release(SOLAR_OS_RESOURCE_I2C_ADDRESS,bus,address,owners[slot]); return false;}
    m.attached=true; return true;
}
bool number(const char *text,long lo,long hi,long &out) {
    if(!text || !*text) return false;
    char *end; out=strtol(text,&end,0); return !*end && out>=lo && out<=hi;
}
}
extern "C" bool sk_motor_shutdown() {
    Guard guard; bool ok=true;
    for(unsigned i=0;i<2;i++) if(motors[i].attached) ok=stop(i) && ok;
    return ok;
}
extern "C" void sk_shell_cmd_motor(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx); io->command_status=1;
    if(argc<3 || (strcmp(argv[2],"slot0") && strcmp(argv[2],"slot1"))) goto usage;
    {
        unsigned slot=argv[2][4]-'0'; Guard guard; auto &m=motors[slot]; bool ok=false;
        if(sk_power_requested()) {solar_os_shell_io_writeln(io,"Shutdown pending");return;}
        if(!strcmp(argv[1],"attach") && (argc==3 || argc==4)) {
            long address=0x10;
            if(argc==4 && !number(argv[3],0x10,0x1f,address)) goto usage;
            ok=attach(slot,address);
        } else if(!m.attached) {solar_os_shell_io_writeln(io,"Attach the motor module first");return;}
        else if(!strcmp(argv[1],"status") && argc==3) {
            ok=read_status(slot);
            if(ok) solar_os_shell_io_printf(io,
                "slot%u i2c%u addr=0x%02x protocol=%u.%u caps=0x%02x armed=%u moving=%u failed=%u error=%u PWM=%u/%u steps=%u lease=%u ms\n",
                slot,superkeyboard::slots[slot].i2c,m.address,m.status[2],m.status[3],m.status[13],
                !!(m.status[4]&8),m.moving,m.failed,m.status[5],m.status[8],m.status[9],m.status[11],unsigned(m.status[14])|(unsigned(m.status[15])<<8));
            else fail(slot);
        } else if(!strcmp(argv[1],"stop") && argc==3) ok=stop(slot);
        else if(!strcmp(argv[1],"detach") && argc==3) {
            ok=stop(slot);
            if(ok) {solar_os_resource_release(SOLAR_OS_RESOURCE_I2C_ADDRESS,superkeyboard::slots[slot].i2c,m.address,owners[slot]);m=Motor{};}
        } else if(!strcmp(argv[1],"arm") && argc==3 && !m.moving) {
            ok=stop(slot) && command(slot,arm_op,1,0xa5); m.failed=!ok;
        } else if(!strcmp(argv[1],"drive") && argc==6) {
            long a,b,duration;
            if(!number(argv[3],-255,255,a) || !number(argv[4],-255,255,b) || !number(argv[5],1,60000,duration)) goto usage;
            if(m.failed || m.moving || !read_status(slot) || !(m.status[4]&8)) goto not_armed;
            // Set direction only at zero duty. Both channels then start together.
            ok=command(slot,0x82,0,0) && command(slot,0xaa,(a<0?2:a>0?1:0)|((b<0?2:b>0?1:0)<<2),0)
               && command(slot,0x82,abs(a),abs(b));
            if(ok) {m.moving=true;m.stepping=false;m.deadline=millis()+duration;m.heartbeat=millis();}
        } else if(!strcmp(argv[1],"step") && argc==6) {
            long direction,period,count;
            if(!number(argv[3],0,1,direction) || !number(argv[4],1,255,period) || !number(argv[5],1,254,count)) goto usage;
            if(m.failed || m.moving || !read_status(slot) || !(m.status[4]&8)) goto not_armed;
            ok=command(slot,0x1a,direction,period) && command(slot,0x1c,count,0);
            if(ok) {m.moving=true;m.stepping=true;m.deadline=millis()+period*count+500;m.heartbeat=millis();}
        } else goto usage;
        if(!ok && m.attached) fail(slot);
        if(worker) xTaskNotifyGive(worker);
        io->command_status=ok?0:1;
        solar_os_shell_io_writeln(io,ok?"motor: ok":"motor: failed; stopped heartbeats, no automatic retry/re-arm");return;
not_armed:
        solar_os_shell_io_writeln(io,"Motor must be idle and explicitly armed; stop an active move first");return;
    }
usage:
    solar_os_shell_io_writeln(io,"motor attach slot0|slot1 [0x10..0x1f]\nmotor status|arm|stop|detach slot0|slot1\nmotor drive slotN DUTY_A DUTY_B DURATION_MS (signed duty -255..255, 1..60000 ms)\nmotor step slotN DIRECTION PERIOD_MS COUNT (0/1, 1..255 ms, 1..254 steps)");
}
#endif
