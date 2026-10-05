#if SK_TELNETD
#include <arduino_freertos.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "network_socket.h"
#include "telnet_console.h"
extern "C" {
#include "solar_os_shell_io.h"
#include "solar_os_telnet_codec.h"
void sk_console_delay_ms(uint32_t);
}
// All access is serialized by the console gate. Only network_socket.cpp touches
// QNEthernet. No client-owned pointer crosses into the network worker.
struct Telnet {
    solar_telnet_codec_t codec;
    bool enabled, authenticated, overflow;
    int fd;
    uint16_t port;
    uint32_t auth_started, connections, failures;
    uint8_t peer[4], input[512];
    size_t head, count, entered;
    char password[64], attempt[64];
};
DMAMEM static Telnet telnet;
extern "C" void sk_telnet_init(void) { memset(&telnet,0,sizeof(telnet)); }
extern "C" bool sk_telnet_enabled(void) { return telnet.enabled; }
extern "C" bool sk_telnet_connected(void) { return telnet.fd && telnet.authenticated; }
extern "C" void sk_telnet_disconnect(void) {
    int fd=telnet.fd;
    telnet.fd=0; telnet.authenticated=false; telnet.head=telnet.count=telnet.entered=0;
    memset(telnet.attempt,0,sizeof(telnet.attempt));
    if(fd) { sk_net_request q{}; sk_net_reply r{}; q.op=SK_NET_CLOSE; q.handle=fd; (void)sk_net_call(&q,&r); }
}
static bool wire(void *,const uint8_t *data,size_t length) {
    const int fd=telnet.fd;
    uint32_t began=millis();
    while(length && fd && fd==telnet.fd) {
        sk_net_request q{}; sk_net_reply r{};
        q.op=SK_NET_SEND; q.handle=fd; q.length=min(length,size_t(SK_NET_CHUNK));
        memcpy(q.data,data,q.length);
        int err=sk_net_call(&q,&r);
        if(err==EAGAIN) {
            if((uint32_t)(millis()-began)>=2000) { sk_telnet_disconnect(); return false; }
            sk_console_delay_ms(2); continue;
        }
        if(err || r.value<=0 || r.value>q.length) { sk_telnet_disconnect(); return false; }
        data+=r.value; length-=r.value;
    }
    return !length;
}
static bool text(const char *s) { return wire(nullptr,(const uint8_t *)s,strlen(s)); }
static bool data(void *,uint8_t b) {
    if(!telnet.authenticated) {
        if(b=='\r') {
            unsigned mismatch=telnet.overflow || telnet.entered!=strlen(telnet.password);
            for(size_t i=0;i<sizeof(telnet.password);++i)mismatch|=(uint8_t)telnet.attempt[i]^(uint8_t)telnet.password[i];
            memset(telnet.attempt,0,sizeof(telnet.attempt)); telnet.entered=0;
            if(mismatch) { ++telnet.failures; text("\r\nAuthentication failed.\r\n"); sk_telnet_disconnect(); return false; }
            telnet.authenticated=true;
            return text("\r\nSolarOS Telnet shell.\r\n");
        }
        if(b==8 || b==127) {
            if(telnet.entered)telnet.attempt[--telnet.entered]=0;
        } else if(b>=32 && b<127 && telnet.entered<sizeof(telnet.attempt)-1)
            telnet.attempt[telnet.entered++]=b;
        else telnet.overflow=true;
        return true;
    }
    if(telnet.count>=sizeof(telnet.input)) { sk_telnet_disconnect(); return false; }
    telnet.input[(telnet.head+telnet.count)%sizeof(telnet.input)]=b; ++telnet.count;
    return true;
}
extern "C" void sk_telnet_poll(bool allow_accept) {
    if(!telnet.enabled)return;
    if(!telnet.fd && allow_accept) {
        sk_net_request q{}; sk_net_reply r{}; q.op=SK_NET_LISTEN_ACCEPT;
        if(sk_net_call(&q,&r))return;
        telnet.fd=r.value; memcpy(telnet.peer,r.ip,4); ++telnet.connections;
        telnet.authenticated=telnet.overflow=false;
        telnet.head=telnet.count=telnet.entered=0; memset(telnet.attempt,0,sizeof(telnet.attempt));
        telnet.auth_started=millis();
        solar_telnet_codec_init(&telnet.codec,wire,data,nullptr);
        if(!solar_telnet_negotiate(&telnet.codec) || !text("SolarOS Telnet\r\nPassword: ")) {
            sk_telnet_disconnect(); return;
        }
    }
    if(!telnet.fd)return;
    if(!telnet.authenticated && (uint32_t)(millis()-telnet.auth_started)>=30000) {
        ++telnet.failures; text("\r\nLogin timed out.\r\n"); sk_telnet_disconnect(); return;
    }
    // Leave space for the worst case: every wire byte is application data.
    if(sizeof(telnet.input)-telnet.count<128)return;
    const int fd=telnet.fd;
    sk_net_request q{}; sk_net_reply r{};
    q.op=SK_NET_RECV; q.handle=fd; q.length=128;
    int err=sk_net_call(&q,&r);
    if(err==EAGAIN)return;
    if(err || !r.value) { sk_telnet_disconnect(); return; }
    for(int i=0;i<r.value && telnet.fd==fd;++i)
        if(!solar_telnet_feed(&telnet.codec,r.data[i])) { sk_telnet_disconnect(); return; }
}
extern "C" int sk_telnet_read(void) {
    if(!telnet.count)return -1;
    int value=telnet.input[telnet.head]; telnet.head=(telnet.head+1)%sizeof(telnet.input); --telnet.count;
    return value;
}
extern "C" bool sk_telnet_write(const uint8_t *p,size_t n) {
    return sk_telnet_connected() && solar_telnet_write(&telnet.codec,p,n);
}
extern "C" void sk_telnet_dimensions(uint16_t *cols,uint16_t *rows) {
    *cols=telnet.codec.cols; *rows=telnet.codec.rows;
}
static bool password_file(const char *path,char *out) {
    if(!path || path[0]!='/')return false;
    FILE *f=fopen(path,"rb"); if(!f)return false;
    unsigned char bytes[67]={}; size_t n=fread(bytes,1,sizeof(bytes),f);
    bool ok=!ferror(f) && feof(f); fclose(f);
    if(n && bytes[n-1]=='\n')--n;
    if(n && bytes[n-1]=='\r')--n;
    if(!n || n>63)ok=false;
    for(size_t i=0;i<n;++i)if(bytes[i]<32 || bytes[i]>=127)ok=false;
    if(ok) { memset(out,0,64); memcpy(out,bytes,n); }
    memset(bytes,0,sizeof(bytes)); return ok;
}
extern "C" void solar_os_shell_cmd_telnetd(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    if(argc==1 || (argc==2 && !strcmp(argv[1],"status"))) {
        solar_os_shell_io_printf(io,"telnetd: %s port=%u client=%s connections=%lu auth-failures=%lu\n",
            telnet.enabled?"running":"stopped",telnet.port,
            sk_telnet_connected()?"authenticated":telnet.fd?"login":"none",
            (unsigned long)telnet.connections,(unsigned long)telnet.failures);
        solar_os_shell_io_printf(io,"console stack: %u bytes%s\n",(unsigned)sk_telnet_console_bytes(),
            !telnet.enabled && sk_telnet_console_bytes()?" (cleanup pending)":"");
        return;
    }
    if(argc==2 && !strcmp(argv[1],"stop")) {
        telnet.enabled=false; sk_telnet_disconnect();
        memset(telnet.password,0,sizeof(telnet.password));
        sk_net_request q{}; sk_net_reply r{}; q.op=SK_NET_LISTEN_STOP; (void)sk_net_call(&q,&r);
        solar_os_shell_io_writeln(io,"telnetd stopped."); return;
    }
    if((argc==3 || argc==4) && !strcmp(argv[1],"start")) {
        if(telnet.enabled) { solar_os_shell_io_writeln(io,"telnetd already running; stop it before changing settings."); return; }
        char secret[64]={};
        unsigned long port=23; char *end=nullptr;
        if(argc==4) {
            port=strtoul(argv[3],&end,10);
            if(!*argv[3] || *end || port==0 || port>65535 || argv[3][0]<'0' || argv[3][0]>'9')goto usage;
        }
        if(!password_file(argv[2],secret)) {
            solar_os_shell_io_writeln(io,"Password file must contain one line of 1-63 printable ASCII characters."); return;
        }
        sk_net_request q{}; sk_net_reply r{}; q.op=SK_NET_LISTEN_START; q.port=port;
        int err=sk_net_call(&q,&r);
        if(!err && !sk_telnet_console_start()) {
            q.op=SK_NET_LISTEN_STOP;(void)sk_net_call(&q,&r);
            solar_os_shell_io_writeln(io,"telnetd: no console memory, or previous session still stopping");
            memset(secret,0,sizeof(secret));return;
        }
        if(err)solar_os_shell_io_printf(io,"telnetd start failed (%d); check network up and port availability.\n",err);
        else { memcpy(telnet.password,secret,sizeof(secret)); telnet.port=port; telnet.enabled=true;
            solar_os_shell_io_printf(io,"telnetd listening on port %u; password required.\n",telnet.port); }
        memset(secret,0,sizeof(secret)); return;
    }
usage:
    solar_os_shell_io_writeln(io,"usage: telnetd start /absolute/password-file [port] | status | stop");
}
#endif
