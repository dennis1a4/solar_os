#if SK_HW_RESOURCES
#include <arduino_freertos.h>
#include <semphr.h>
#include "platform.h"
#include "serial_terminal.h"
extern "C" {
#include "solar_os.h"
#include "solar_os_buses.h"
#include "solar_os_uart.h"
#include "solar_os_memory.h"
#include "solar_os_shell.h"
#include "solar_os_shell_io.h"
#include "solar_os_storage.h"
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>

// The capture task never takes the filesystem/lifecycle lock. SD latency can
// fill the PSRAM queue but cannot stall UART polling behind a file write.
struct Ring {
    uint8_t *data=nullptr; size_t capacity=0, head=0, used=0;
    size_t put(const uint8_t *p,size_t n) {
        n=std::min(n,capacity-used);
        if (!n) return 0;
        size_t tail=(head+used)%capacity, first=std::min(n,capacity-tail);
        memcpy(data+tail,p,first);memcpy(data,p+first,n-first);
        used+=n;return n;
    }
    size_t get(uint8_t *p,size_t n) {
        n=std::min(n,used);
        if (!n) return 0;
        size_t first=std::min(n,capacity-head);
        memcpy(p,data+head,first);memcpy(p+first,data,n-first);
        head=(head+n)%capacity;used-=n;return n;
    }
};
struct Channel {
    bool active=false, attached=false, timestamped=false;
    bool tx_paused=false, rx_paused=false;
    FILE *file=nullptr; Ring log,view;
    uint64_t rx=0,tx=0,log_dropped=0,view_dropped=0,stored=0,storage_lost=0;
    uint32_t started=0; esp_err_t error=ESP_OK;
    char path[SOLAR_OS_STORAGE_PATH_MAX]{};
    const char *owner=nullptr;
};
static Channel channels[3];
struct Settings { uint32_t baud=0; uint16_t format=0; char framing[4]="8N1"; bool software_flow=false; };
static Settings settings[3];
static bool parse_format(const char *s,uint16_t &format){
    if(strlen(s)!=3 || (s[0]!='7' && s[0]!='8') || (s[2]!='1' && s[2]!='2'))return false;
    if(s[1]=='N' && s[0]=='8')format=0;
    else if(s[1]=='E' || s[1]=='O')format=(s[0]=='8'?6:2)+(s[1]=='O');
    else return false;
    if(s[2]=='2')format|=0x100;
    return true;
}
static const char *const names[]={"uart7","uart8","uart3"};
static StaticSemaphore_t state_storage,life_storage;
static SemaphoreHandle_t state_lock,life_lock;
static StaticTask_t capture_tcb,writer_tcb;
DMAMEM static StackType_t capture_stack[1024],writer_stack[1536];
struct Lock { SemaphoreHandle_t m; explicit Lock(SemaphoreHandle_t s):m(s){xSemaphoreTake(m,portMAX_DELAY);} ~Lock(){xSemaphoreGive(m);} };
static int index_of(const char *name){for(int i=0;i<3;++i)if(name && !strcmp(name,names[i]))return i;return -1;}
static bool allocate(Ring &r,size_t n){r.data=(uint8_t *)solar_os_memory_alloc(n,SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"serial.buffer");r.capacity=r.data?n:0;return r.data;}
static void release(Ring &r){solar_os_memory_free(r.data);r=Ring{};}
static void enqueue(Channel &c,const uint8_t *data,size_t n,bool tx){
    if(!c.file || (!c.timestamped && tx))return;
    if(c.error!=ESP_OK){c.log_dropped+=n;return;}
    if(!c.timestamped){c.log_dropped+=n-c.log.put(data,n);return;}
    // Chunk timestamps are milliseconds since recording began, not per-byte
    // hardware timestamps. Whole records are dropped on overflow.
    while(n){
        size_t count=std::min(n,size_t(128));char line[410];
        int pos=snprintf(line,sizeof(line),"%lu %s ",(unsigned long)(millis()-c.started),tx?"TX":"RX");
        static const char hex[]="0123456789abcdef";
        for(size_t j=0;j<count;++j){line[pos++]=hex[data[j]>>4];line[pos++]=hex[data[j]&15];}
        line[pos++]='\n';
        if(c.log.capacity-c.log.used>=size_t(pos))c.log.put((uint8_t *)line,pos);else c.log_dropped+=count;
        data+=count;n-=count;
    }
}
// XON/XOFF only interprets 0x11/0x13 when explicitly enabled. Raw logs
// retain them. Flow-control transmissions bypass the peer's TX pause.
static void update_flow(int i){
    auto &c=channels[i];if(!settings[i].software_flow)return;
    bool high=c.file ? c.error!=ESP_OK || c.log.used>=49152 : c.view.used>=3072;
    bool low=c.file ? c.error==ESP_OK && c.log.used<=32768 : c.view.used<=2048;
    bool pause=c.rx_paused ? !low : high;
    if(pause!=c.rx_paused){uint8_t control=pause?0x13:0x11;size_t n=0;
        solar_os_bus_uart_write(names[i],&control,1,&n);
        if(n){c.rx_paused=pause;c.tx+=n;enqueue(c,&control,n,true);}
    }
}
static void capture_once(){
    uint8_t input[128];Lock lock(state_lock);
    for(int i=0;i<3;++i){auto &c=channels[i];if(!c.active)continue;
        // Bound work per port so a saturated UART cannot starve other ports.
        for(unsigned chunk=0;chunk<16;++chunk){size_t n=0;
            esp_err_t e=solar_os_bus_uart_read(names[i],input,sizeof(input),0,&n);
            if(e!=ESP_OK){c.error=e;break;}if(!n)break;
            c.rx+=n;enqueue(c,input,n,false);
            if(settings[i].software_flow){
                size_t visible=0;
                for(size_t j=0;j<n;++j){
                    if(input[j]==0x13)c.tx_paused=true;
                    else if(input[j]==0x11)c.tx_paused=false;
                    else input[visible++]=input[j];
                }
                n=visible;
            }
            if(c.attached)c.view_dropped+=n-c.view.put(input,n);
        }
        update_flow(i);
    }
}
static void capture_task(void *){for(;;){capture_once();vTaskDelay(1);}}
static void write_chunk(Channel &c,const uint8_t *data,size_t n){
    size_t written=fwrite(data,1,n,c.file);
    Lock lock(state_lock);c.stored+=written;
    if(written!=n){c.error=ESP_FAIL;c.storage_lost+=n-written;}
}
static void writer_task(void *){
    uint8_t output[1024];uint32_t flushed=millis();
    for(;;){
        {Lock life(life_lock);
            for(auto &c:channels){
                if(!c.file)continue;
                size_t n=0;{Lock state(state_lock);if(c.error==ESP_OK)n=c.log.get(output,sizeof(output));}
                if(n)write_chunk(c,output,n);
                if(uint32_t(millis()-flushed)>=1000 && fflush(c.file)!=0){Lock state(state_lock);c.error=ESP_FAIL;}
            }
            if(uint32_t(millis()-flushed)>=1000)flushed=millis();
        }
        vTaskDelay(1);
    }
}
extern "C" void sk_serial_init(){
    state_lock=xSemaphoreCreateMutexStatic(&state_storage);life_lock=xSemaphoreCreateMutexStatic(&life_storage);
    configASSERT(state_lock && life_lock);
    configASSERT(xTaskCreateStatic(capture_task,"serial-rx",1024,nullptr,2,capture_stack,&capture_tcb));
    configASSERT(xTaskCreateStatic(writer_task,"serial-log",1536,nullptr,1,writer_stack,&writer_tcb));
}
static esp_err_t open_channel(int i,uint32_t baud,const char *owner){
    auto &c=channels[i];
    if(c.active){solar_os_uart_status_t s{};solar_os_uart_get_bus_status(names[i],&s);return !baud || baud==s.baud_rate?ESP_OK:ESP_ERR_INVALID_STATE;}
    if(!baud)baud=settings[i].baud;
    if(!baud){solar_os_uart_status_t s{};solar_os_uart_get_bus_status(names[i],&s);baud=s.baud_rate;}
    esp_err_t e=sk_uart_claim_format(i,owner,baud,settings[i].format);if(e!=ESP_OK)return e;
    c=Channel{};c.owner=owner;c.active=true;return ESP_OK;
}
static void close_unused(int i){
    auto &c=channels[i];if(!c.active || c.attached || c.file)return;
    if(c.rx_paused){
        // Make room for a final XON even if application TX filled the hardware
        // queue. No CTS gating is enabled; at 300 baud a frame is at most 40 ms.
        uint8_t xon=0x11;size_t sent=0;
        for(unsigned attempt=0;attempt<100 && !sent;++attempt){
            solar_os_bus_uart_write(names[i],&xon,1,&sent);
            if(!sent)vTaskDelay(1);
        }
        c.tx+=sent;
        if(!sent)c.error=ESP_ERR_TIMEOUT;
        c.rx_paused=false;
    }
    sk_uart_release(i,c.owner);c.active=false;
}
extern "C" esp_err_t sk_serial_attach(const char *name,uint32_t baud){
    int i=index_of(name);if(i<0)return ESP_ERR_NOT_FOUND;
    Lock life(life_lock);Lock state(state_lock);auto &c=channels[i];
    if(c.attached)return ESP_ERR_INVALID_STATE;
    esp_err_t e=open_channel(i,baud,"app-com");if(e!=ESP_OK)return e;
    if(!allocate(c.view,4096)){close_unused(i);return ESP_ERR_NO_MEM;}
    c.attached=true;return ESP_OK;
}
extern "C" void sk_serial_detach(const char *name){
    int i=index_of(name);if(i<0)return;Lock life(life_lock);Lock state(state_lock);
    auto &c=channels[i];c.attached=false;release(c.view);close_unused(i);
}
extern "C" esp_err_t sk_serial_read(const char *name,uint8_t *data,size_t capacity,size_t *received){
    if(received)*received=0;
    int i=index_of(name);if(i<0 || !data || !received)return ESP_ERR_INVALID_ARG;
    Lock state(state_lock);auto &c=channels[i];if(!c.attached)return ESP_ERR_INVALID_STATE;
    *received=c.view.get(data,capacity);return ESP_OK;
}
extern "C" esp_err_t sk_serial_write(const char *name,const uint8_t *data,size_t length,size_t *written){
    if(written)*written=0;
    int i=index_of(name);if(i<0 || !data || !written)return ESP_ERR_INVALID_ARG;
    Lock state(state_lock);auto &c=channels[i];if(!c.active)return ESP_ERR_INVALID_STATE;
    if(c.tx_paused)return ESP_ERR_TIMEOUT;
    esp_err_t e=solar_os_bus_uart_write(name,data,length,written);c.tx+=*written;enqueue(c,data,*written,true);return e;
}
static esp_err_t start_log(int i,uint32_t baud,const char *path,bool timestamped){
    Lock life(life_lock);
    {Lock state(state_lock);if(channels[i].file)return ESP_ERR_INVALID_STATE;}
    Ring ring;if(!allocate(ring,65536))return ESP_ERR_NO_MEM;
    FILE *file=fopen(path,"wbx");if(!file){release(ring);return ESP_FAIL;}
    setvbuf(file,nullptr,_IONBF,0);
    esp_err_t e;
    {Lock state(state_lock);auto &c=channels[i];e=open_channel(i,baud,"serial-log");
        if(e==ESP_OK){
            c.log=ring;c.file=file;c.timestamped=timestamped;c.started=millis();c.error=ESP_OK;c.log_dropped=0;c.stored=0;c.storage_lost=0;
            strlcpy(c.path,path,sizeof(c.path));
        }
    }
    if(e!=ESP_OK){fclose(file);unlink(path);release(ring);}
    return e;
}
static esp_err_t stop_log(int i){
    Lock life(life_lock);FILE *file;Ring remaining;
    {Lock state(state_lock);auto &c=channels[i];if(!c.file)return ESP_ERR_INVALID_STATE;
        file=c.file;c.file=nullptr;remaining=c.log;c.log=Ring{};close_unused(i);}
    uint8_t output[1024];auto &c=channels[i];size_t n;
    while((n=remaining.get(output,sizeof(output)))){
        size_t w=fwrite(output,1,n,file);{Lock state(state_lock);c.stored+=w;if(w!=n){c.error=ESP_FAIL;c.storage_lost+=n-w+remaining.used;}}if(w!=n)break;
    }
    int result=fclose(file);release(remaining);Lock state(state_lock);if(result)c.error=ESP_FAIL;return c.error;
}
extern "C" void sk_serial_settings(const char *name,char *out,size_t capacity){
    int i=index_of(name);if(i<0){if(capacity)*out=0;return;}
    Lock state(state_lock);snprintf(out,capacity,"%s flow=%s",settings[i].framing,settings[i].software_flow?"xonxoff":"none");
}
extern "C" void sk_shell_cmd_serial(solar_os_context_t *ctx,int argc,char **argv){
    auto *io=solar_os_context_shell_io(ctx);io->command_status=1;
    if(argc==1 || (argc==2 && !strcmp(argv[1],"status"))){
        for(int i=0;i<3;++i){Channel c;Settings config;{Lock state(state_lock);c=channels[i];config=settings[i];}
            solar_os_shell_io_printf(io,"%s %s flow=%s TX-paused=%s configured-baud=%lu\n",names[i],config.framing,config.software_flow?"xonxoff":"none",c.tx_paused?"yes":"no",(unsigned long)config.baud);
            solar_os_shell_io_printf(io,"%s: %s terminal=%s RX=%llu TX=%llu log=%u/65536 view=%u/4096 dropped-log=%llu dropped-view=%llu stored=%llu storage-lost=%llu error=%s\n",names[i],c.file?"recording":c.active?"open":"closed",c.attached?"yes":"no",(unsigned long long)c.rx,(unsigned long long)c.tx,(unsigned)c.log.used,(unsigned)c.view.used,(unsigned long long)c.log_dropped,(unsigned long long)c.view_dropped,(unsigned long long)c.stored,(unsigned long long)c.storage_lost,esp_err_to_name(c.error));
            if(c.path[0])solar_os_shell_io_printf(io,"  %s (%s)\n",c.path,c.timestamped?"timestamped RX/TX hex":"raw RX");
        }
        solar_os_shell_io_writeln(io,"Hardware UART overruns are not measured. Timestamps are chunk arrival milliseconds.");io->command_status=0;return;
    }
    int i=argc>=3?index_of(argv[2]):-1;esp_err_t e=ESP_ERR_INVALID_ARG;
    if(i>=0 && (argc==5 || argc==6) && !strcmp(argv[1],"config")){
        char *end=nullptr;unsigned long baud=strtoul(argv[3],&end,10);uint16_t format;
        if(!*argv[3] || *end || baud<300 || baud>1000000 || !parse_format(argv[4],format) ||
           (argc==6 && strcmp(argv[5],"none") && strcmp(argv[5],"xonxoff")))goto usage;
        Lock life(life_lock);Lock state(state_lock);solar_os_uart_status_t status{};
        solar_os_uart_get_bus_status(names[i],&status);
        if(channels[i].active || status.initialized)e=ESP_ERR_INVALID_STATE;
        else {settings[i].baud=baud;settings[i].format=format;strlcpy(settings[i].framing,argv[4],4);settings[i].software_flow=argc==6 && !strcmp(argv[5],"xonxoff");e=ESP_OK;}
    }
    else if(i>=0 && argc==3 && !strcmp(argv[1],"stop")){
        e=stop_log(i);Channel snapshot;{Lock state(state_lock);snapshot=channels[i];}
        solar_os_shell_io_printf(io,"serial: log-dropped=%llu storage-lost=%llu stored=%llu\n",(unsigned long long)snapshot.log_dropped,(unsigned long long)snapshot.storage_lost,(unsigned long long)snapshot.stored);
    }
    else if(i>=0 && (argc==5 || argc==6) && !strcmp(argv[1],"record")){
        char *end=nullptr;unsigned long baud=strtoul(argv[3],&end,10);
        if(!*argv[3] || *end || baud<300 || baud>1000000 || (argc==6 && strcmp(argv[5],"--timestamp")))goto usage;
        char path[SOLAR_OS_STORAGE_PATH_MAX];if(solar_os_shell_resolve_path(ctx,argv[4],path,sizeof(path))!=ESP_OK)return;
        e=start_log(i,baud,path,argc==6);
    }else goto usage;
    solar_os_shell_io_printf(io,"serial: %s\n",esp_err_to_name(e));io->command_status=e==ESP_OK?0:1;return;
usage:
    solar_os_shell_io_writeln(io,"usage: serial config BUS BAUD 8N1|8N2|7E1|7E2|7O1|7O2|8E1|8E2|8O1|8O2 [none|xonxoff] (closed ports only) | serial status | serial record uart7|uart8|uart3 BAUD NEWFILE [--timestamp] | serial stop BUS");
}
#endif
