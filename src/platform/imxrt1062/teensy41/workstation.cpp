#if SK_UPSTREAM_SHELL
#include <arduino_freertos.h>
extern "C" {
#include "solar_os.h"
#include "solar_os_shell.h"
#include "solar_os_shell_io.h"
#include "solar_os_shell_commands.h"
#include "solar_os_storage.h"
#include "solar_os_memory.h"
#include "solar_os_port.h"
#if SK_CLOCK
#include "solar_os_time.h"
#endif
}
#ifndef SOLAR_OS_VERSION
#define SOLAR_OS_VERSION "development"
#endif

static bool no_args(solar_os_shell_io_t *io,int argc,char **argv) {
    if(argc==1)return true;
    solar_os_shell_io_printf(io,"usage: %s\n",argv[0]); return false;
}
extern "C" void solar_os_shell_cmd_version(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if(no_args(io,argc,argv))solar_os_shell_io_printf(io,"SolarOS %s / Teensy 4.1 port\n",SOLAR_OS_VERSION);
}
extern "C" void solar_os_shell_cmd_board(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if(no_args(io,argc,argv))solar_os_shell_io_printf(io,
        "Teensy 4.1 / i.MX RT1062 / Cortex-M7 / %lu MHz\n",(unsigned long)(F_CPU_ACTUAL/1000000));
}
extern "C" void sk_shell_cmd_pwd(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if(!no_args(io,argc,argv))return;
    char path[SOLAR_OS_STORAGE_PATH_MAX];
    if(solar_os_shell_resolve_path(ctx,nullptr,path,sizeof(path))==ESP_OK)solar_os_shell_io_writeln(io,path);
}
extern "C" void solar_os_shell_cmd_status(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if(!no_args(io,argc,argv))return;
    solar_os_shell_cmd_version(ctx,1,argv);
    solar_os_shell_cmd_board(ctx,1,argv);
    solar_os_shell_cmd_uptime(ctx,1,argv);
    solar_os_shell_cmd_mem(ctx,1,argv);
    solar_os_shell_io_printf(io,"Mounted volumes: %u; FreeRTOS tasks: %u\n",
        (unsigned)solar_os_storage_mount_count(),(unsigned)uxTaskGetNumberOfTasks());
    solar_os_shell_io_printf(io,"Last foreground exit: %d\n",
        solar_os_shell_session_last_exit_code(solar_os_context_shell_session(ctx)));
}
extern "C" void solar_os_shell_cmd_top(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if(!no_args(io,argc,argv))return;
    // Spare slots tolerate task creation between sizing and the atomic snapshot.
    const UBaseType_t capacity=uxTaskGetNumberOfTasks()+8;
    struct Snapshot { char name[configMAX_TASK_NAME_LEN]; };
    auto *tasks=static_cast<TaskStatus_t *>(solar_os_memory_calloc(capacity,sizeof(TaskStatus_t),
        SOLAR_OS_MEMORY_EXTERNAL_PREFERRED,"top.snapshot"));
    if(!tasks) { solar_os_shell_io_writeln(io,"top: no memory");return; }
    auto *names=static_cast<Snapshot *>(solar_os_memory_calloc(capacity,sizeof(Snapshot),
        SOLAR_OS_MEMORY_EXTERNAL_PREFERRED,"top.names"));
    if(!names) { solar_os_memory_free(tasks);solar_os_shell_io_writeln(io,"top: no memory");return; }
    configRUN_TIME_COUNTER_TYPE total=0;
    vTaskSuspendAll();
    const UBaseType_t count=uxTaskGetSystemState(tasks,capacity,&total);
    for(UBaseType_t i=0;i<count;++i)strlcpy(names[i].name,tasks[i].pcTaskName,sizeof(names[i].name));
    xTaskResumeAll();
    solar_os_shell_io_writeln(io,"Task                 State Pri Stack-free(bytes) CPU(cumulative)");
    for(UBaseType_t i=0;i<count;++i) {
        auto &t=tasks[i];
        const char *state=t.eCurrentState==eRunning?"run":t.eCurrentState==eReady?"ready":
            t.eCurrentState==eBlocked?"wait":t.eCurrentState==eSuspended?"suspend":"deleted";
        const unsigned tenths=total ? (uint64_t(t.ulRunTimeCounter)*1000/total) : 0;
        solar_os_shell_io_printf(io,"%-20s %-7s %u %lu %u.%u%%\n",names[i].name,state,
            (unsigned)t.uxCurrentPriority,(unsigned long)(t.usStackHighWaterMark*sizeof(StackType_t)),tenths/10,tenths%10);
    }
    if(!count)solar_os_shell_io_writeln(io,"top: task list changed; retry");
    solar_os_memory_free(tasks);
    solar_os_memory_free(names);
}
extern "C" void solar_os_shell_cmd_port(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if(argc>2 || (argc==2 && strcmp(argv[1],"list"))) {
        solar_os_shell_io_writeln(io,"usage: port [list]");return;
    }
    solar_os_port_info_t ports[8];
    const size_t count=solar_os_port_list(ports,8);
    for(size_t i=0;i<count && i<8;++i)solar_os_shell_io_printf(io,"%-10s %-16s %s\n",
        ports[i].name,ports[i].claimed?ports[i].owner:"free",ports[i].label);
}
extern "C" void solar_os_shell_cmd_df(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if(!no_args(io,argc,argv))return;
    solar_os_shell_io_writeln(io,"Mount       Total(KiB)    Used(KiB)    Free(KiB)");
    solar_os_storage_mount_info_t mount;
    for(size_t i=0;solar_os_storage_get_mount(i,&mount);++i) {
        solar_os_storage_usage_t usage;
        esp_err_t err=solar_os_storage_get_usage_for_path(mount.mount_point,&usage);
        if(err==ESP_OK)solar_os_shell_io_printf(io,"%-10s %12llu %12llu %12llu\n",mount.mount_point,
            (unsigned long long)(usage.total_bytes/1024),(unsigned long long)(usage.used_bytes/1024),
            (unsigned long long)(usage.free_bytes/1024));
        else solar_os_shell_io_printf(io,"%s: %s\n",mount.mount_point,esp_err_to_name(err));
    }
}
#if SK_CLOCK
static unsigned digits(const char *s,size_t n) {
    unsigned value=0;while(n--)value=value*10+(*s++-'0');return value;
}
static void datetime_command(solar_os_context_t *ctx,int argc,char **argv,bool date) {
    auto *io=solar_os_context_shell_io(ctx);
    solar_os_datetime_t value{};
    esp_err_t err=solar_os_time_get_datetime(&value);
    if(argc>2) { solar_os_shell_io_printf(io,"usage: %s [%s]\n",argv[0],date?"YYYY-MM-DD":"HH:MM[:SS]");return; }
    if(argc==2) {
        if(err!=ESP_OK) { solar_os_shell_io_writeln(io,"Clock invalid; set UTC first with rtc set UNIX_SECONDS");return; }
        const char *s=argv[1];
        size_t n=strlen(s);
        bool valid=date?n==10:(n==5 || n==8);
        for(size_t i=0;valid && i<n;++i) {
            bool separator=date?(i==4 || i==7):(i==2 || i==5);
            valid=separator?s[i]==(date?'-':':'):(s[i]>='0' && s[i]<='9');
        }
        if(!valid) { solar_os_shell_io_writeln(io,"Invalid date/time syntax");return; }
        if(date) {
            value.year=digits(s,4);value.month=digits(s+5,2);value.day=digits(s+8,2);
        } else {
            value.hour=digits(s,2);value.minute=digits(s+3,2);value.second=n==8?digits(s+6,2):0;
        }
        err=solar_os_time_set_datetime(&value);
    }
    if(err!=ESP_OK) { solar_os_shell_io_printf(io,"%s: %s\n",argv[0],esp_err_to_name(err));return; }
    if(date)solar_os_shell_io_printf(io,"%04u-%02u-%02u\n",value.year,value.month,value.day);
    else solar_os_shell_io_printf(io,"%02u:%02u:%02u\n",value.hour,value.minute,value.second);
}
extern "C" void solar_os_shell_cmd_date(solar_os_context_t *ctx,int argc,char **argv) { datetime_command(ctx,argc,argv,true); }
extern "C" void solar_os_shell_cmd_time(solar_os_context_t *ctx,int argc,char **argv) { datetime_command(ctx,argc,argv,false); }
#endif
#endif
