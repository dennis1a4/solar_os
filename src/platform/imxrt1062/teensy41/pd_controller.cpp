#if SK_POWER && SK_HW_RESOURCES
#include <arduino_freertos.h>
#include <semphr.h>
#include "platform.h"
extern "C" {
#include "solar_os.h"
#include "solar_os_shell_io.h"
#include "solar_os_resources.h"
#include "solar_os_stusb4500.h"
#include "solar_os_task.h"
}
static solar_stusb4500_t controller;
static solar_pd_power_t power;
static StaticSemaphore_t guard_storage;
static SemaphoreHandle_t guard;
static TaskHandle_t worker;
static unsigned bus,address;
static volatile bool stopping,done;
static bool read_register(void *,uint8_t reg,uint8_t *out,size_t n){return sk_i2c_transfer(bus,address,&reg,1,out,n)==ESP_OK;}
static bool write_register(void *,uint8_t reg,const uint8_t *data,size_t n){
    if(n>28)return false;uint8_t bytes[29];bytes[0]=reg;memcpy(bytes+1,data,n);
    return sk_i2c_transfer(bus,address,bytes,n+1,nullptr,0)==ESP_OK;
}
static void run(void *){
    while(!stopping){xSemaphoreTake(guard,portMAX_DELAY);solar_pd_power_poll(&power,millis());xSemaphoreGive(guard);vTaskDelay(1);}
    done=true;for(;;)vTaskSuspend(nullptr);
}
static bool number(const char *s,unsigned low,unsigned high,unsigned &out){
    if(!s || !*s || *s=='-')return false;char *end;unsigned long value=strtoul(s,&end,0);
    if(*end || value<low || value>high)return false;out=value;return true;
}
extern "C" void sk_shell_cmd_pd(solar_os_context_t *ctx,int argc,char **argv){
    auto *io=solar_os_context_shell_io(ctx);io->command_status=1;
    if(!guard)guard=xSemaphoreCreateMutexStatic(&guard_storage);
    if(argc==1 || (argc==2 && !strcmp(argv[1],"status"))){
        if(!worker){solar_os_shell_io_writeln(io,"USB-PD: unconfigured; STUSB4500 hardware validation pending.");io->command_status=0;return;}
        xSemaphoreTake(guard,portMAX_DELAY);auto copy=power;xSemaphoreGive(guard);
        solar_os_shell_io_printf(io,"STUSB4500 i2c%u address=0x%02x; %s; board limits=%u mV/%u mA\n",bus,address,solar_pd_power_state_name(copy.state),copy.max_mv,copy.max_ma);
        for(unsigned i=0;i<copy.source.count;++i)solar_os_shell_io_printf(io,"  %u mV / %u mA\n",copy.source.profiles[i].mv,copy.source.profiles[i].ma);
        if(copy.source.contract_valid)solar_os_shell_io_printf(io,"contract: %u mV / %u mA (not measured current)\n",copy.source.contract.mv,copy.source.contract.ma);
        io->command_status=0;return;
    }
    if(argc==2 && !strcmp(argv[1],"close")){
        if(worker){stopping=true;while(!solar_os_task_wait_done(worker,&done,1000))vTaskDelay(1);solar_os_task_delete_external(worker);worker=nullptr;solar_os_resource_release(SOLAR_OS_RESOURCE_I2C_ADDRESS,bus,address,"usb-pd");}
        solar_os_shell_io_writeln(io,"PD monitoring closed; controller retains its negotiated voltage.");io->command_status=0;return;
    }
    if(argc==6 && !strcmp(argv[1],"open")){
        unsigned next_bus,next_address,mv,ma;
        if(worker){solar_os_shell_io_writeln(io,"pd: already open");return;}
        if(strlen(argv[2])!=4 || strncmp(argv[2],"i2c",3) || !number(argv[2]+3,0,2,next_bus) || !number(argv[3],0x28,0x2b,next_address) || !number(argv[4],5000,20000,mv) || !number(argv[5],100,5000,ma))goto usage;
        bus=next_bus;address=next_address;
        if(solar_os_resource_claim(SOLAR_OS_RESOURCE_I2C_ADDRESS,bus,address,"usb-pd","STUSB4500")!=ESP_OK){solar_os_shell_io_writeln(io,"pd: address is reserved");return;}
        solar_stusb4500_io_t transport{read_register,write_register,nullptr};
        bool ok=solar_stusb4500_init(&controller,&transport,mv,ma);
        if(ok){
            // Explicit open starts discovery with only a 5 V / 100 mA sink PDO.
            uint32_t pdo=(100UL<<10)|10;uint8_t bytes[4]={uint8_t(pdo),uint8_t(pdo>>8),0,0},one=1,header[2]={13,0},command=0x26;
            bytes[2]=uint8_t(pdo>>16);
            ok=write_register(nullptr,0x85,bytes,4) && write_register(nullptr,0x70,&one,1) && write_register(nullptr,0x51,header,2) && write_register(nullptr,0x1a,&command,1);
        }
        if(ok){solar_pd_power_init(&power,&solar_stusb4500_backend,&controller,mv,ma);stopping=done=false;ok=solar_os_task_create_pinned_external(run,"usb-pd",4096,nullptr,1,&worker,0,SOLAR_OS_TASK_ROLE_SYSTEM)==pdPASS;}
        if(!ok){solar_os_resource_release(SOLAR_OS_RESOURCE_I2C_ADDRESS,bus,address,"usb-pd");solar_os_shell_io_writeln(io,"pd: initialization failed");return;}
        solar_os_shell_io_writeln(io,"PD monitoring started; 5 V discovery requested. Use pd status.");io->command_status=0;return;
    }
    if(argc==4 && !strcmp(argv[1],"request")){
        unsigned mv,ma;if(!worker || !number(argv[2],5000,20000,mv) || !number(argv[3],10,5000,ma))goto usage;
        xSemaphoreTake(guard,portMAX_DELAY);bool ok=false;
        for(unsigned i=0;i<power.source.count;++i)if(power.source.profiles[i].mv==mv){ok=solar_pd_power_request(&power,i,ma,millis());break;}
        xSemaphoreGive(guard);io->command_status=ok?0:1;
        solar_os_shell_io_writeln(io,ok?"PD request sent; use pd status to check the confirmed contract.":"pd: unavailable profile, limit exceeded, or negotiation busy");return;
    }
usage:
    solar_os_shell_io_writeln(io,"usage: pd status|close | pd open i2cN ADDRESS BOARD_MAX_MV BOARD_MAX_MA | pd request MV MA");
}
#endif
