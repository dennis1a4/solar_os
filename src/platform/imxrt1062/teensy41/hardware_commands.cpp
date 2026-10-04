#if SK_HW_RESOURCES
#include <arduino_freertos.h>
#include "board.h"
#include "platform.h"
#include "net_diagnostics_protocol.h"
extern "C" {
#include "solar_os_shell_commands.h"
#include "solar_os_shell_io.h"
#include "solar_os_resources.h"
#include "solar_os_uart.h"
#include "solar_os_buses.h"
#include "hardware_commands.h"
#include "solar_os_io.h"
#include "solar_os_shell_completion.h"
bool sk_console_is_local(void);
bool sk_console_is_remote(void);
bool sk_console_poll_cancel(bool);
}
static uint8_t gpio_mode[42]; // 0 unconfigured, 1 input, 2 output
static const char *owner() {
    if(sk_console_is_local())return "hw:lcd";
#if SK_TELNETD
    if(sk_console_is_remote())return "hw:telnet";
#endif
    return "hw:usb";
}
static const char *gpio_owner() {
    if(sk_console_is_local())return "gpio:lcd";
#if SK_TELNETD
    if(sk_console_is_remote())return "gpio:telnet";
#endif
    return "gpio:usb";
}
static bool number(const char *s,uint32_t lo,uint32_t hi,uint32_t &n) {
    if(s && s[0]=='0' && (s[1]=='x' || s[1]=='X')) {
        s+=2;n=0;if(!*s)return false;
        for(;*s;++s) {
            unsigned d=*s>='0' && *s<='9'?*s-'0':*s>='a' && *s<='f'?*s-'a'+10:*s>='A' && *s<='F'?*s-'A'+10:16;
            if(d>15 || d>hi || n>(hi-d)/16)return false;n=n*16+d;
        }
        return n>=lo;
    }
    return skdiag::number(s,lo,hi,n);
}
static bool slot_index(const char *s,unsigned &slot) {
    uint32_t n;if(!s || strncmp(s,"slot",4) || !number(s+4,0,2,n))return false;slot=n;return true;
}
static bool uart_slot(const char *s,unsigned &slot) {
    for(unsigned i=0;i<3;++i) {char name[16];snprintf(name,sizeof(name),"uart%u",superkeyboard::slots[i].uart);if(s && !strcmp(s,name)){slot=i;return true;}}
    return false;
}
static bool pin_owned(unsigned pin) {
    solar_os_resource_claim_t claim{};
    return solar_os_resource_find_claim(SOLAR_OS_RESOURCE_GPIO_PIN,pin,-1,&claim) && !strcmp(claim.owner,gpio_owner());
}
static void status(solar_os_shell_io_t *io,const char *name,esp_err_t e) {
    solar_os_shell_io_printf(io,"%s: %s\n",name,esp_err_to_name(e));
}
static void pin_status(solar_os_shell_io_t *io,unsigned pin) {
    solar_os_resource_claim_t c{};
    if(solar_os_resource_find_claim(SOLAR_OS_RESOURCE_GPIO_PIN,pin,-1,&c))
        solar_os_shell_io_printf(io,"%2u %-18s %s\n",pin,c.owner,c.label);
    else solar_os_shell_io_printf(io,"%2u free\n",pin);
}
static void list_pins(solar_os_shell_io_t *io) {for(unsigned i=0;i<42;++i)pin_status(io,i);}
static void list_claims(solar_os_shell_io_t *io) {
    solar_os_resource_claim_t c{};
    for(size_t i=0;i<solar_os_resource_claim_count();++i)if(solar_os_resource_get_claim(i,&c))
        solar_os_shell_io_printf(io,"%-12s %2d %3d %-18s %s\n",solar_os_resource_kind_name(c.kind),c.primary,c.secondary,c.owner,c.label);
}
static void list_buses(solar_os_shell_io_t *io) {
    solar_os_shell_io_writeln(io,"i2c0 SDA18 SCL19; i2c1 SDA17 SCL16; i2c2 SDA25 SCL24; fixed 100000 Hz");
    solar_os_shell_io_writeln(io,"spi0 MOSI11 MISO12 SCK13; spi1 MOSI26 MISO39 SCK27; transfer through owned slot CS");
    for(unsigned i=0;i<3;++i) {
        char name[16];snprintf(name,sizeof(name),"uart%u",superkeyboard::slots[i].uart);
        solar_os_uart_status_t u{};solar_os_uart_get_bus_status(name,&u);
        solar_os_shell_io_printf(io,"%s RX%d TX%d %lu 8N1 owner=%s\n",name,u.rx_pin,u.tx_pin,(unsigned long)u.baud_rate,u.port_claimed?u.port_owner:"free");
    }
}
void sk_hardware_release(const char *who) {
    char gpio_who[24];snprintf(gpio_who,sizeof(gpio_who),"gpio%s",who+2);
    for(unsigned pin=0;pin<42;++pin) {
        solar_os_resource_claim_t c{};
        if(gpio_mode[pin] && solar_os_resource_find_claim(SOLAR_OS_RESOURCE_GPIO_PIN,pin,-1,&c) && !strcmp(c.owner,gpio_who)) {
            pinMode(pin,INPUT);gpio_mode[pin]=0;solar_os_resource_release(SOLAR_OS_RESOURCE_GPIO_PIN,pin,-1,gpio_who);
        }
    }
    for(unsigned i=0;i<3;++i) {sk_uart_release(i,who);if(!strcmp(sk_slot_owner(i),who))sk_slot_release(i,who);}
}
extern "C" void sk_hardware_console_release() {sk_hardware_release(owner());}
extern "C" void sk_shell_cmd_io(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if(argc==1 || (argc==2 && !strcmp(argv[1],"pins")))list_pins(io);
    else if(argc==2 && !strcmp(argv[1],"claims"))list_claims(io);
    else if(argc==2 && !strcmp(argv[1],"buses"))list_buses(io);
    else if(argc==2 && !strcmp(argv[1],"release")){sk_hardware_release(owner());solar_os_shell_io_writeln(io,"io: released this console's GPIO, UART and expansion claims");}
    else solar_os_shell_io_writeln(io,"usage: io [pins|claims|buses|release]; use gpio/i2c/spi/uart/expansion to configure");
}
extern "C" void solar_os_shell_cmd_gpio(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);uint32_t pin,value=0;
    if(argc==1 || (argc==2 && !strcmp(argv[1],"list"))){list_pins(io);return;}
    if(argc==2 && number(argv[1],0,41,pin)){pin_status(io,pin);return;}
    if(argc<3 || !number(argv[2],0,41,pin))goto usage;
    if(!strcmp(argv[1],"mode") && (argc==4 || argc==5)) {
        int mode;
        if(!strcmp(argv[3],"in"))mode=INPUT;
        else if(!strcmp(argv[3],"pullup"))mode=INPUT_PULLUP;
        else if(!strcmp(argv[3],"pulldown"))mode=INPUT_PULLDOWN;
        else if(!strcmp(argv[3],"out"))mode=OUTPUT;
        else goto usage;
        if(argc==5 && (mode!=OUTPUT || !number(argv[4],0,1,value)))goto usage;
        // A console's UART/slot claim is not a GPIO-mode claim even if the owner
        // string matches. Do not remux a pin belonging to another local function.
        solar_os_resource_claim_t c{};
        if(solar_os_resource_find_claim(SOLAR_OS_RESOURCE_GPIO_PIN,pin,-1,&c) &&
            (!gpio_mode[pin] || strcmp(c.owner,gpio_owner()))) {
            solar_os_shell_io_printf(io,"gpio: pin %lu busy: %s (%s)\n",(unsigned long)pin,c.owner,c.label);return;
        }
        esp_err_t e=solar_os_resource_claim(SOLAR_OS_RESOURCE_GPIO_PIN,pin,-1,gpio_owner(),"GPIO");
        if(e==ESP_OK) {if(mode==OUTPUT)digitalWrite(pin,value);pinMode(pin,mode);gpio_mode[pin]=mode==OUTPUT?2:1;}
        status(io,"gpio",e);return;
    }
    if(!strcmp(argv[1],"read") && argc==3) {
        if(!gpio_mode[pin] || !pin_owned(pin))status(io,"gpio",ESP_ERR_INVALID_STATE);
        else solar_os_shell_io_printf(io,"GPIO%lu=%d\n",(unsigned long)pin,digitalRead(pin));return;
    }
    if(!strcmp(argv[1],"write") && argc==4 && number(argv[3],0,1,value)) {
        if(gpio_mode[pin]!=2 || !pin_owned(pin))status(io,"gpio",ESP_ERR_INVALID_STATE);
        else {digitalWrite(pin,value);status(io,"gpio",ESP_OK);}return;
    }
    if(!strcmp(argv[1],"release") && argc==3) {
        if(!gpio_mode[pin] || !pin_owned(pin))status(io,"gpio",ESP_ERR_INVALID_STATE);
        else {pinMode(pin,INPUT);gpio_mode[pin]=0;status(io,"gpio",solar_os_resource_release(SOLAR_OS_RESOURCE_GPIO_PIN,pin,-1,gpio_owner()));}return;
    }
usage:
    solar_os_shell_io_writeln(io,"usage: gpio [list|PIN] | gpio mode PIN in|pullup|pulldown|out [0|1] | gpio read PIN | gpio write PIN 0|1 | gpio release PIN");
}
static bool hexbytes(const char *text,uint8_t *bytes,size_t limit,size_t &len) {
    len=0;if(!strcmp(text,"-"))return true;
    const size_t n=strlen(text);if(n%2 || n/2>limit)return false;
    for(size_t i=0;i<n;i+=2) {char pair[]={ '0','x',text[i],text[i+1],0};uint32_t value;if(!number(pair,0,255,value))return false;bytes[len++]=value;}
    return true;
}
extern "C" void solar_os_shell_cmd_i2c(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);uint32_t bus,address,count;
    if(argc==1 || (argc==2 && !strcmp(argv[1],"list"))){list_buses(io);return;}
    if(argc<3 || strncmp(argv[2],"i2c",3) || !number(argv[2]+3,0,2,bus))goto usage;
    if(argc==3 && !strcmp(argv[1],"scan")) {
        unsigned found=0,skipped=0;
        for(unsigned a=8;a<=0x77;++a) {
            if(sk_console_poll_cancel(true)){solar_os_shell_io_writeln(io,"i2c: stopped");break;}
            if(solar_os_resource_claim(SOLAR_OS_RESOURCE_I2C_ADDRESS,bus,a,owner(),"probe")!=ESP_OK){++skipped;continue;}
            esp_err_t e=sk_i2c_transfer(bus,a,nullptr,0,nullptr,0);
            solar_os_resource_release(SOLAR_OS_RESOURCE_I2C_ADDRESS,bus,a,owner());
            if(e==ESP_OK){++found;solar_os_shell_io_printf(io,"i2c%lu 0x%02x\n",(unsigned long)bus,a);}
        }
        solar_os_shell_io_printf(io,"i2c: %u found, %u reserved addresses skipped\n",found,skipped);return;
    }
    if(argc==6 && !strcmp(argv[1],"xfer") && number(argv[3],8,0x77,address) && number(argv[5],0,32,count)) {
        uint8_t tx[32],rx[32];size_t n;
        if(!hexbytes(argv[4],tx,32,n))goto usage;
        esp_err_t e=solar_os_resource_claim(SOLAR_OS_RESOURCE_I2C_ADDRESS,bus,address,owner(),"transfer");
        if(e==ESP_OK) {e=sk_i2c_transfer(bus,address,tx,n,rx,count);solar_os_resource_release(SOLAR_OS_RESOURCE_I2C_ADDRESS,bus,address,owner());}
        status(io,"i2c",e);if(e==ESP_OK && count){for(unsigned i=0;i<count;++i)solar_os_shell_io_printf(io,"%02x",rx[i]);solar_os_shell_io_newline(io);}return;
    }
usage:
    solar_os_shell_io_writeln(io,"usage: i2c [list] | i2c scan i2c0|i2c1|i2c2 | i2c xfer BUS ADDRESS HEX|- RX_COUNT (max 32 bytes)");
}
extern "C" void solar_os_shell_cmd_expansion(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);unsigned slot;
    if(argc==1 || (argc==2 && !strcmp(argv[1],"list"))) {
        for(unsigned i=0;i<3;++i) {auto p=superkeyboard::slots[i];solar_os_shell_io_printf(io,"slot%u SPI%u CS%u I2C%u UART%u RX%u TX%u owner=%s\n",i,p.spi,p.cs,p.i2c,p.uart,p.rx,p.tx,*sk_slot_owner(i)?sk_slot_owner(i):"free");}return;
    }
    if(argc==3 && slot_index(argv[2],slot)) {
        if(!strcmp(argv[1],"claim")){status(io,"expansion",sk_slot_claim(slot,owner()));return;}
        if(!strcmp(argv[1],"release")){status(io,"expansion",sk_slot_release(slot,owner()));return;}
    }
    solar_os_shell_io_writeln(io,"usage: expansion [list] | expansion claim|release slot0|slot1|slot2 (SPI CS lease; UART leased separately)");
}
extern "C" void solar_os_shell_cmd_spi(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);unsigned slot;uint32_t hz,mode;
    if(argc==1 || (argc==2 && !strcmp(argv[1],"list"))){list_buses(io);return;}
    if(argc==6 && !strcmp(argv[1],"xfer") && slot_index(argv[2],slot) && number(argv[3],0,3,mode) && number(argv[4],100000,12000000,hz)) {
        uint8_t tx[128],rx[128];size_t len;
        if(hexbytes(argv[5],tx,128,len) && len) {
            esp_err_t e=sk_slot_spi(slot,owner(),hz,mode,tx,rx,len);status(io,"spi",e);
            if(e==ESP_OK){for(size_t i=0;i<len;++i)solar_os_shell_io_printf(io,"%02x",rx[i]);solar_os_shell_io_newline(io);}return;
        }
    }
    solar_os_shell_io_writeln(io,"usage: spi [list] | spi xfer SLOT MODE HZ HEX (claim expansion slot first; mode 0..3, 100000..12000000 Hz, <=128 bytes)");
}
extern "C" void solar_os_shell_cmd_uart(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);unsigned slot;uint32_t baud=115200,count=32;
    if(argc==1 || (argc==2 && !strcmp(argv[1],"list"))){list_buses(io);return;}
    if(argc<3 || !uart_slot(argv[2],slot))goto usage;
    if(!strcmp(argv[1],"open") && (argc==3 || (argc==4 && number(argv[3],300,1000000,baud)))) {status(io,"uart",sk_uart_claim(slot,owner(),baud));return;}
    if(!strcmp(argv[1],"close") && argc==3){status(io,"uart",sk_uart_release(slot,owner()));return;}
    if(!strcmp(argv[1],"read") && (argc==3 || (argc==4 && number(argv[3],1,128,count)))) {
        solar_os_uart_status_t state{};solar_os_uart_get_bus_status(argv[2],&state);
        if(strcmp(state.port_owner,owner())){status(io,"uart",ESP_ERR_INVALID_STATE);return;}
        unsigned n=0;for(;n<count;++n){int ch=sk_slot_uart_read(slot,owner());if(ch<0)break;solar_os_shell_io_printf(io,"%02x",ch);}
        solar_os_shell_io_printf(io,"\nuart: %u bytes (nonblocking)\n",n);return;
    }
    if(!strcmp(argv[1],"write") && argc==4) {
        solar_os_uart_status_t state{};solar_os_uart_get_bus_status(argv[2],&state);
        if(strcmp(state.port_owner,owner())){status(io,"uart",ESP_ERR_INVALID_STATE);return;}
        size_t written=0,length=strlen(argv[3]);
        esp_err_t e=solar_os_bus_uart_write(argv[2],reinterpret_cast<const uint8_t *>(argv[3]),length,&written);
        status(io,"uart",e);solar_os_shell_io_printf(io,"uart: %u/%u bytes queued\n",unsigned(written),unsigned(length));return;
    }
usage:
    solar_os_shell_io_writeln(io,"usage: uart [list] | uart open uart7|uart8|uart3 [300..1000000] | uart close BUS | uart read BUS [1..128] | uart write BUS TEXT");
}
extern "C" bool sk_hardware_complete(const solar_os_completion_request_t *r,solar_os_completion_emit_t emit,void *sink,void *) {
    char tokens[4][64];size_t starts[4],count=0;bool trailing=false;
    if(!solar_os_shell_completion_parse(r->line,r->start,&tokens[0][0],64,4,starts,&count,&trailing) || !count)return true;
    const char *values=nullptr;
    if(r->argument==1) {
        if(!strcmp(tokens[0],"gpio"))values="list mode read write release";
        else if(!strcmp(tokens[0],"io"))values="pins claims buses release";
        else if(!strcmp(tokens[0],"uart"))values="list open close read write";
        else if(!strcmp(tokens[0],"com"))values="uart7 uart8 uart3 --hex --baud --enter";
        else if(!strcmp(tokens[0],"serial"))values="status config record stop";
        else if(!strcmp(tokens[0],"expansion"))values="list claim release";
        else if(!strcmp(tokens[0],"i2c"))values="list scan xfer";
        else if(!strcmp(tokens[0],"spi"))values="list xfer";
    } else if(r->argument==2) {
        if(!strcmp(tokens[0],"uart") || !strcmp(tokens[0],"serial") || !strcmp(tokens[0],"com"))values="uart7 uart8 uart3";
        else if(!strcmp(tokens[0],"i2c"))values="i2c0 i2c1 i2c2";
        else if(!strcmp(tokens[0],"spi") || !strcmp(tokens[0],"expansion"))values="slot0 slot1 slot2";
        else if(!strcmp(tokens[0],"gpio")) {
            for(unsigned i=0;i<42;++i){char pin[4];snprintf(pin,sizeof(pin),"%u",i);if(!emit(sink,pin))return false;}
        }
    } else if(r->argument==3 && !strcmp(tokens[0],"gpio") && !strcmp(tokens[1],"mode"))values="in out pullup pulldown";
    if(values) {
        while(*values) {
            const char *end=strchr(values,' ');size_t len=end?size_t(end-values):strlen(values);
            char value[24];memcpy(value,values,len);value[len]=0;if(!emit(sink,value))return false;
            values+=len;if(*values)++values;
        }
    }
    return true;
}
static esp_err_t io_start(solar_os_context_t *ctx) {
    char *args[2];int argc=solar_os_context_argc(ctx);
    for(int i=0;i<argc && i<2;++i)args[i]=const_cast<char *>(solar_os_context_argv(ctx,i));
    sk_shell_cmd_io(ctx,argc,args);solar_os_context_finish(ctx,0,nullptr);return ESP_OK;
}
extern "C" const solar_os_app_t solar_os_io_app={
    .name="io",.summary="inspect hardware resources",.app_class=SOLAR_OS_APP_CLASS_TUI,
    .flags=SOLAR_OS_APP_FLAG_SHELL_INLINE,.start=io_start
};
#endif
