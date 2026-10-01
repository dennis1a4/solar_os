#if SK_RAMFS
#include "storage_lock.h"
extern "C" {
#include "solar_os_ramfs.h"
#include "solar_os_memory.h"
#include "solar_os_shell_io.h"
#include "solar_os_completion.h"
#include "solar_os_shell_completion.h"
}
#include <string.h>
#include <stdint.h>
static bool quota(const char *text,size_t &value) {
    value=0; if(!text || !*text)return false;
    while(*text>='0' && *text<='9') { unsigned d=*text++-'0'; if(value>(SIZE_MAX-d)/10)return false; value=value*10+d; }
    size_t factor=1;
    if(*text=='k' || *text=='K') {factor=1024; ++text;}
    else if(*text=='m' || *text=='M') {factor=1024*1024; ++text;}
    if(*text || value>SIZE_MAX/factor)return false;
    value*=factor; return value>=1024 && value<=4*1024*1024;
}
static void status(solar_os_shell_io_t *io) {
    size_t count=solar_os_ramfs_mount_count();
    if(!count)solar_os_shell_io_writeln(io,"ramfs: no mounts");
    for(size_t i=0;i<count;++i) {
        solar_os_ramfs_info_t info{}; if(!solar_os_ramfs_get_info(i,&info))continue;
        solar_os_shell_io_printf(io,"%s total=%lu used=%lu free=%lu files=%u dirs=%u open=%u (volatile PSRAM)\n",info.mount_point,
            (unsigned long)info.total_bytes,(unsigned long)info.used_bytes,(unsigned long)info.free_bytes,
            unsigned(info.file_count),unsigned(info.dir_count),unsigned(info.open_count));
    }
}
extern "C" void solar_os_shell_cmd_ramfs(solar_os_context_t *ctx,int argc,char **argv) {
    StorageLock lock; auto *io=solar_os_context_shell_io(ctx);
    if(argc==1 || (argc==2 && !strcmp(argv[1],"status"))) {status(io);return;}
    esp_err_t result;
    if(argc==4 && !strcmp(argv[1],"mount")) {
        size_t bytes;
        if(!quota(argv[3],bytes)) {solar_os_shell_io_writeln(io,"ramfs: size must be 1024..4194304 bytes, optionally k/m");return;}
        solar_os_memory_status_t memory{};solar_os_memory_get_status(&memory);
        if(memory.external.free<bytes+512*1024) {solar_os_shell_io_writeln(io,"ramfs: insufficient PSRAM (512 KiB app reserve)");return;}
        result=solar_os_ramfs_mount(argv[2],bytes);
    } else if(argc==3 && !strcmp(argv[1],"unmount")) result=solar_os_ramfs_unmount(argv[2]);
    else {solar_os_shell_io_writeln(io,"usage: ramfs [status] | ramfs mount /NAME SIZE | ramfs unmount /NAME");return;}
    solar_os_shell_io_printf(io,"ramfs: %s\n",esp_err_to_name(result));
    if(result==ESP_OK)status(io);
    else if(result==ESP_ERR_INVALID_STATE)solar_os_shell_io_writeln(io,"ramfs: mount exists or is busy; close files and directory handles first");
}
extern "C" bool sk_ramfs_complete(const solar_os_completion_request_t *r,solar_os_completion_emit_t emit,void *sink,void *) {
    StorageLock lock;
    if(r->argument==1) {for(auto s:{"status","mount","unmount"})if(!emit(sink,s))return false;}
    else if(r->argument==2) {
        solar_os_ramfs_info_t info{};
        for(size_t i=0;solar_os_ramfs_get_info(i,&info);++i)if(!emit(sink,info.mount_point))return false;
    } else if(r->argument==3) {for(auto s:{"64k","256k","1m","4m"})if(!emit(sink,s))return false;}
    return true;
}
#endif
