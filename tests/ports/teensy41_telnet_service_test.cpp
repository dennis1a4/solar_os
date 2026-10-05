#include "telnet_console.h"
#include "network_socket.h"
extern "C" {
#include "solar_os_shell_io.h"
}
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <deque>
#include <string>
#include <unistd.h>
static uint32_t now;
static bool console_failure;
extern "C" bool sk_telnet_console_start(void) { return !console_failure; }
extern "C" size_t sk_telnet_console_bytes(void) { return sk_telnet_enabled()?40960:0; }
static bool listening, queued, live, backpressure;
static std::deque<uint8_t> rx;
static std::string tx, output;
uint32_t millis() { return now; }
extern "C" void sk_console_delay_ms(uint32_t n) { now+=n; }
extern "C" solar_os_shell_io_t *solar_os_context_shell_io(solar_os_context_t *) { return nullptr; }
extern "C" esp_err_t solar_os_shell_io_writeln(solar_os_shell_io_t *,const char *s) { output+=s; return ESP_OK; }
extern "C" esp_err_t solar_os_shell_io_printf(solar_os_shell_io_t *,const char *fmt,...) {
    char text[512]; va_list args; va_start(args,fmt); vsnprintf(text,sizeof(text),fmt,args); va_end(args); output+=text; return ESP_OK;
}
extern "C" int sk_net_call(sk_net_request *q,sk_net_reply *r) {
    *r={};
    switch(q->op) {
    case SK_NET_LISTEN_START: if(listening)return EALREADY; listening=true; return 0;
    case SK_NET_LISTEN_STOP: listening=live=false; return 0;
    case SK_NET_LISTEN_ACCEPT: if(!queued || !listening)return EAGAIN; queued=false; live=true; r->value=7; return 0;
    case SK_NET_CLOSE: live=false; return 0;
    case SK_NET_SEND:
        if(!live)return EPIPE;
        if(backpressure)return EAGAIN;
        r->value=q->length>7?7:q->length; // force partial writes
        tx.append((char *)q->data,r->value); return 0;
    case SK_NET_RECV:
        if(!live)return 0;
        if(rx.empty())return EAGAIN;
        while(r->value<q->length && !rx.empty()) { r->data[r->value++]=rx.front(); rx.pop_front(); }
        return 0;
    default: assert(false); return EINVAL;
    }
}
static void cmd(const char *verb,const char *path=nullptr,const char *port=nullptr) {
    char name[]="telnetd"; char *args[]={name,const_cast<char *>(verb),const_cast<char *>(path),const_cast<char *>(port)};
    output.clear(); solar_os_shell_cmd_telnetd(nullptr,port?4:path?3:2,args);
}
static void incoming(const std::string &s) { for(uint8_t b:s)rx.push_back(b); sk_telnet_poll(false); }
static void accept() { rx.clear(); tx.clear(); queued=true; sk_telnet_poll(true); assert(tx.find("Password: ")!=std::string::npos); }
int main() {
    char path[]="/tmp/solaros-telnet-pass-XXXXXX"; int fd=mkstemp(path); assert(fd>=0);
    FILE *f=fdopen(fd,"w"); fputs("host-test-secret\n",f); fclose(f);
    sk_telnet_init(); cmd("start",path,"0"); assert(!listening);
    cmd("start","/missing"); assert(!listening);
    console_failure=true;cmd("start",path);assert(!listening && !sk_telnet_enabled());
    console_failure=false;
    cmd("start",path); assert(listening && output.find("listening")!=std::string::npos);
    accept(); incoming("wrong\r\n"); assert(!live && !sk_telnet_connected() && tx.find("Authentication failed")!=std::string::npos);
    accept(); incoming("host-test-secreX\bt\r\n"); assert(sk_telnet_connected());
    assert(tx.find("host-test-secret")==std::string::npos);
    incoming("echo hello\r\n"); std::string line; for(int c;(c=sk_telnet_read())>=0;)line+=(char)c;
    assert(line=="echo hello\r");
    const uint8_t text[]={'a',255,'\n'}; assert(sk_telnet_write(text,sizeof(text)));
    cmd("stop"); assert(!live && !listening && !sk_telnet_connected());
    cmd("start",path); accept(); incoming(std::string(100,'x')+"\r"); assert(!live);
    accept(); now+=30001; sk_telnet_poll(false); assert(!live);
    accept(); incoming("host-test-secret\r"); assert(sk_telnet_connected());
    backpressure=true; assert(!sk_telnet_write(text,sizeof(text)) && !live); backpressure=false;
    for(unsigned i=0;i<100;++i) {
        accept(); incoming("host-test-secret\r\n"); assert(sk_telnet_connected());
        live=false; sk_telnet_poll(false); assert(!sk_telnet_connected());
    }
    cmd("stop"); assert(!listening); unlink(path);
    puts("telnet service: auth, overflow, timeout, partial writes, backpressure, reconnect passed");
}
