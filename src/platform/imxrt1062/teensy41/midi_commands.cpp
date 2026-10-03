#if SK_MIDI
#include <arduino_freertos.h>
#include "platform.h"
#include "midi_record.h"
extern "C" {
#include "solar_os.h"
#include "solar_os_shell_commands.h"
#include "solar_os_shell.h"
#include "solar_os_shell_io.h"
#include "solar_os_buses.h"
#include "solar_os_storage.h"
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
}
extern "C" bool sk_console_poll_cancel(bool);
extern bool sk_midi_usb_connected();
extern void sk_midi_usb_capture(bool);
extern bool sk_midi_usb_pop(MidiEvent &);
extern unsigned sk_midi_usb_drops();
extern unsigned sk_midi_usb_unsupported();
extern bool sk_midi_usb_send(const solar_os_midi_message_t &);
static bool midi_busy;
static int endpoint(const char *name){
    if(!strcmp(name,"usb"))return 3;
    if(strlen(name)==5 && !strncmp(name,"slot",4) && name[4]>='0' && name[4]<='2')return name[4]-'0';
    return -1;
}
static const char *const buses[]={"uart7","uart8","uart3"};
static esp_err_t open_endpoint(int port){return port==3?(sk_midi_usb_connected()?ESP_OK:ESP_ERR_NOT_FOUND):sk_uart_claim(port,"midi",31250);}
static bool send_message(int port,const solar_os_midi_message_t &m){
    if(port==3)return sk_midi_usb_send(m);
    uint8_t bytes[3];size_t sent=0,n=solar_os_midi_encode(&m,bytes);
    uint32_t started=millis();size_t offset=0;
    while(offset<n){
        esp_err_t err=solar_os_bus_uart_write(buses[port],bytes+offset,n-offset,&sent);
        offset+=sent;
        if(err!=ESP_OK && err!=ESP_ERR_TIMEOUT)return false;
        if(millis()-started>100)return false;
        if(offset<n)vTaskDelay(1);
    }
    return n!=0;
}
extern "C" void sk_shell_cmd_midi(solar_os_context_t *ctx,int argc,char **argv){
    auto *io=solar_os_context_shell_io(ctx);io->command_status=1;
    if(argc==1 || (argc==2 && !strcmp(argv[1],"status"))){
        solar_os_shell_io_printf(io,"USB MIDI: %s; UART MIDI: slot0/slot1/slot2, 31250 baud; operation=%s\n",sk_midi_usb_connected()?"connected":"absent",midi_busy?"busy":"idle");
        io->command_status=0;return;
    }
    bool record=argc==4 && !strcmp(argv[1],"record"),play=argc==4 && !strcmp(argv[1],"play");
    int port=argc==4?endpoint(record?argv[2]:argv[3]):-1;
    if((!record && !play)||port<0){solar_os_shell_io_writeln(io,"usage: midi status | midi record usb|slotN NEWFILE.smr | midi play FILE.smr usb|slotN");return;}
    if(midi_busy){solar_os_shell_io_writeln(io,"midi: another MIDI operation is active");return;}
    char path[SOLAR_OS_STORAGE_PATH_MAX];
    if(solar_os_shell_resolve_path(ctx,record?argv[3]:argv[2],path,sizeof(path))!=ESP_OK)return;
    struct stat metadata;
    if(record && (stat(path,&metadata)==0 || errno!=ENOENT)){solar_os_shell_io_writeln(io,"midi: output already exists");return;}
    FILE *file=fopen(path,record?"wbx":"rb");if(!file){solar_os_shell_io_writeln(io,"midi: cannot open file");return;}
    midi_busy=true;esp_err_t err=open_endpoint(port);uint32_t count=0,unsupported=0;bool cancelled=false;
    bool opened=err==ESP_OK;
    const uint8_t magic[4]={'S','M','R','1'};uint8_t data[8];
    if(opened){
        if(record){if(fwrite(magic,1,4,file)!=4)err=ESP_FAIL;}
        else if(fread(data,1,4,file)!=4 || memcmp(data,magic,4))err=ESP_ERR_INVALID_ARG;
    }
    if(err==ESP_OK){
        solar_os_shell_io_writeln(io,record?"MIDI recording; Ctrl+C to finish. SysEx is not recorded.":"MIDI playback; Ctrl+C to stop.");
        uint32_t start=millis(),previous=0;solar_os_midi_decoder_t decoder{};
        if(record && port==3)sk_midi_usb_capture(true);
        while(err==ESP_OK){
            if(sk_console_poll_cancel(true)){cancelled=true;break;}
            if(port==3 && !sk_midi_usb_connected()){err=ESP_ERR_NOT_FOUND;break;}
            if(record){
                unsigned received=0;
                if(port==3){
                    MidiEvent e{};
                    while(received<128 && sk_midi_usb_pop(e)){
                        ++received;e.ms-=start;midi_pack(e,data);
                        if(fwrite(data,1,8,file)!=8){err=ESP_FAIL;break;}++count;
                    }
                }else{
                    uint8_t input[128];size_t got=0;
                    err=solar_os_bus_uart_read(buses[port],input,sizeof(input),0,&got);
                    received=got;
                    for(size_t i=0;i<got;++i){
                        MidiEvent e{millis()-start,{}};
                        auto result=solar_os_midi_decode_byte(&decoder,input[i],&e.message);
                        if(result==SOLAR_OS_MIDI_DECODE_UNSUPPORTED)++unsupported;
                        if(result==SOLAR_OS_MIDI_DECODE_MESSAGE && e.message.status!=0xf7){
                            midi_pack(e,data);if(fwrite(data,1,8,file)!=8){err=ESP_FAIL;break;}++count;
                        }
                    }
                }
                if(!received)vTaskDelay(1);
                if(uint32_t(millis()-start)>=24*60*60*1000UL){err=ESP_ERR_INVALID_SIZE;break;}
            }else{
                size_t n=fread(data,1,8,file);if(!n){if(ferror(file))err=ESP_FAIL;break;}
                MidiEvent e{};if(n!=8 || !midi_unpack(data,e) || e.ms<previous || e.ms>24*60*60*1000UL){err=ESP_ERR_INVALID_ARG;break;}
                previous=e.ms;
                while(uint32_t(millis()-start)<e.ms){if(sk_console_poll_cancel(true)){cancelled=true;break;}vTaskDelay(1);}
                if(cancelled)break;
                if(!send_message(port,e.message)){err=ESP_FAIL;break;}++count;
            }
        }
    }
    if(record && port==3){sk_midi_usb_capture(false);unsupported+=sk_midi_usb_unsupported();if(sk_midi_usb_drops())err=ESP_ERR_NO_MEM;}
    if(play && opened && count){for(unsigned ch=0;ch<16;++ch){
        for(uint8_t control:{uint8_t(64),uint8_t(123)}){solar_os_midi_message_t m{uint8_t(0xb0|ch),control,0,3};(void)send_message(port,m);}
    }}
    if(opened && port!=3)sk_uart_release(port,"midi");
    if(fclose(file)!=0)err=ESP_FAIL;
    if(record && !count && err!=ESP_OK)(void)unlink(path);
    midi_busy=false;io->command_status=err==ESP_OK?0:1;
    solar_os_shell_io_printf(io,"midi: %lu events; unsupported=%lu; %s%s\n",(unsigned long)count,(unsigned long)unsupported,esp_err_to_name(err),cancelled?" (stopped)":"");
}
#endif
