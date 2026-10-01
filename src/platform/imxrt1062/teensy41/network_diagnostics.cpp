#if SK_NET_DIAGNOSTICS
#include <arduino_freertos.h>
#include <errno.h>
#include "network_socket.h"
#include "net_diagnostics_protocol.h"
extern "C" {
#include "solar_os_shell_commands.h"
#include "solar_os_shell_io.h"
#include "solar_os_net_session.h"
#include "solar_os_time.h"
bool sk_console_poll_cancel(bool);
void sk_clock_rtc_set(uint32_t);
}
namespace {
struct Cancel {bool stopped=false;};
bool cancel(void *arg){auto &c=*static_cast<Cancel *>(arg);if(!c.stopped)c.stopped=sk_console_poll_cancel(true);return c.stopped;}
void address(uint32_t ip,char *out,size_t size){snprintf(out,size,"%u.%u.%u.%u",unsigned(ip>>24),unsigned(ip>>16&255),unsigned(ip>>8&255),unsigned(ip&255));}
bool resolve(const char *host,uint8_t out[4],Cancel &c,solar_os_shell_io_t *io) {
    int err=sk_net_resolve(host,out,5000,cancel,&c);
    if(err)solar_os_shell_io_printf(io,"network: %s (%d)\n",c.stopped?"stopped":"resolution failed",err);
    return !err;
}
}
extern "C" void solar_os_shell_cmd_ping(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);uint32_t count=4;
    if(argc<2 || argc>3 || (argc==3 && !skdiag::number(argv[2],1,999,count))){solar_os_shell_io_writeln(io,"usage: ping HOST [1..999] (default 4; Ctrl+C/Esc stops)");return;}
    Cancel c;uint8_t ip[4];if(!resolve(argv[1],ip,c,io))return;
    solar_os_shell_io_printf(io,"ping %u.%u.%u.%u: %lu requests, 16 data bytes\n",ip[0],ip[1],ip[2],ip[3],(unsigned long)count);
    uint32_t sent=0,received=0,min=UINT32_MAX,max=0;uint64_t total=0;
    for(uint32_t seq=1;seq<=count && !cancel(&c);++seq) {
        sk_net_request q{};sk_net_reply r{};q.op=SK_NET_PING_START;memcpy(q.ip,ip,4);
        uint32_t tag=micros();memcpy(q.data,&tag,4);memcpy(q.data+4,"SolarOS ping",12);
        int error=sk_net_call(&q,&r);
        if(error){solar_os_shell_io_printf(io,"ping: start failed (%d)%s\n",error,error==EBUSY?"; another ping is active":"");break;}
        ++sent;const int token=r.value;uint32_t start=millis(),elapsed=0;bool reply=false;
        q.op=SK_NET_PING_POLL;q.handle=token;
        while(millis()-start<1000 && !cancel(&c)) {
            error=sk_net_call(&q,&r);if(!error){reply=true;elapsed=r.value;break;}if(error!=EAGAIN)break;
        }
        uint8_t ttl=r.data[0];q.op=SK_NET_PING_CLOSE;sk_net_reply closed{};sk_net_call(&q,&closed);
        if(reply){++received;total+=elapsed;if(elapsed<min)min=elapsed;if(elapsed>max)max=elapsed;
            solar_os_shell_io_printf(io,"reply seq=%lu ttl=%u time=%lu.%03lu ms\n",(unsigned long)seq,ttl,(unsigned long)(elapsed/1000),(unsigned long)(elapsed%1000));}
        else if(!c.stopped)solar_os_shell_io_printf(io,"%s seq=%lu\n",error!=EAGAIN && error?"network error":"timeout",(unsigned long)seq);
        if(error && error!=EAGAIN)break;
        while(seq<count && millis()-start<1000 && !cancel(&c)){}
    }
    solar_os_shell_io_printf(io,"ping: %lu sent, %lu received, %lu%% loss%s\n",(unsigned long)sent,(unsigned long)received,(unsigned long)(sent?(sent-received)*100/sent:0),c.stopped?", stopped":"");
    if(received)solar_os_shell_io_printf(io,"rtt min/avg/max = %lu/%lu/%lu us\n",(unsigned long)min,(unsigned long)(total/received),(unsigned long)max);
}
extern "C" void solar_os_shell_cmd_netscan(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);uint16_t ports[128];size_t n=0;
    if(argc<2 || argc>3 || !skdiag::ports(argc==3?argv[2]:"22,23,53,80,443,1883,8080",ports,n)) {
        solar_os_shell_io_writeln(io,"usage: netscan HOST|A.B.C.D-E|CIDR [PORTS] (IPv4 /24..32; max 128 ports)");return;}
    uint32_t first,count;Cancel c;
    if(!skdiag::targets(argv[1],first,count)) {
        if(strchr(argv[1],'/')){solar_os_shell_io_writeln(io,"netscan: invalid IPv4 range (/24..32 only)");return;}
        uint8_t ip[4];if(!resolve(argv[1],ip,c,io))return;first=uint32_t(ip[0])<<24|uint32_t(ip[1])<<16|uint32_t(ip[2])<<8|ip[3];count=1;
    }
    solar_os_net_session_t *session=nullptr;
    esp_err_t err=solar_os_net_session_create("netscan",cancel,&c,&session);
    if(err!=ESP_OK){solar_os_shell_io_printf(io,"netscan: %s\n",esp_err_to_name(err));return;}
    solar_os_shell_io_printf(io,"netscan: %lu hosts, %u ports; TCP connect, 350 ms per port; Ctrl+C/Esc stops\n",(unsigned long)count,unsigned(n));
    uint32_t probes=0,open=0;bool failed=false;
    for(uint32_t host=0;host<count && !c.stopped && !failed;++host) {
        char ip[20];address(first+host,ip,sizeof(ip));
        for(size_t i=0;i<n && !cancel(&c);++i) {
            uint32_t handle=0;++probes;
            err=solar_os_net_session_tcp_connect(session,ip,ports[i],350,&handle);
            if(err==ESP_OK){++open;solar_os_shell_io_printf(io,"%-15s %5u open\n",ip,ports[i]);solar_os_net_session_close(session,handle);}
            else if(err==ESP_ERR_NO_MEM || err==ESP_ERR_INVALID_STATE){failed=true;solar_os_shell_io_printf(io,"netscan: network/session error: %s\n",esp_err_to_name(err));break;}
        }
    }
    solar_os_net_session_destroy(session);
    solar_os_shell_io_printf(io,"netscan: %lu open, %lu probes%s\n",(unsigned long)open,(unsigned long)probes,c.stopped?", stopped":"");
}
extern "C" void solar_os_shell_cmd_ntp(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);bool query=argc>1 && !strcmp(argv[1],"-q");int index=query?2:1;uint32_t port=123;
    if(argc>index+2 || (argc==index+2 && !skdiag::number(argv[index+1],1,65535,port))){solar_os_shell_io_writeln(io,"usage: ntp [-q] [SERVER [PORT]] (-q queries without setting RTC)");return;}
    const char *host=argc>index?argv[index]:SOLAR_OS_NTP_DEFAULT_SERVER;
    Cancel c;uint8_t ip[4];if(!resolve(host,ip,c,io))return;char target[20];address(uint32_t(ip[0])<<24|uint32_t(ip[1])<<16|uint32_t(ip[2])<<8|ip[3],target,sizeof(target));
    solar_os_net_session_t *session=nullptr;uint32_t handle=0;
    esp_err_t err=solar_os_net_session_create("ntp",cancel,&c,&session);
    uint8_t packet[48]={};packet[0]=0x23;
    // The shared TRNG can return a partial buffer while it refills. Keep its
    // Ethernet-task ownership and yield the console while collecting the nonce.
    uint32_t entropy_start=millis();size_t filled=0;
    while(err==ESP_OK && filled<8) {
        if(cancel(&c)){err=ESP_ERR_INVALID_STATE;break;}
        if(millis()-entropy_start>=1000){err=ESP_ERR_TIMEOUT;break;}
        sk_net_request random{};sk_net_reply entropy{};
        random.op=SK_NET_ENTROPY;random.length=8-filled;
        if(sk_net_call(&random,&entropy) || entropy.value<0 || size_t(entropy.value)>8-filled){err=ESP_FAIL;break;}
        memcpy(packet+40+filled,entropy.data,entropy.value);filled+=entropy.value;
    }
    if(err==ESP_OK)err=solar_os_net_session_udp_open(session,0,&handle);
    uint32_t start=millis();uint64_t epoch=0;bool valid=false,denied=false;unsigned rejected=0;
    if(err==ESP_OK)err=solar_os_net_session_udp_send(session,handle,target,port,packet,sizeof(packet),1000);
    while(err==ESP_OK && millis()-start<5000 && !cancel(&c)) {
        uint8_t reply[96];solar_os_net_receive_result_t result{};
        err=solar_os_net_session_udp_receive(session,handle,reply,sizeof(reply),50,&result);
        if(err!=ESP_OK)break;if(result.timed_out)continue;
        if(result.truncated || strcmp(result.address,target) || result.port!=port){++rejected;continue;}
        auto state=skdiag::ntp(reply,result.data_len,packet+40,millis()-start,epoch);
        if(state==skdiag::NtpReply::Valid){valid=true;break;}
        if(state==skdiag::NtpReply::Denied){denied=true;break;}
        ++rejected;
    }
    solar_os_net_session_destroy(session);
    if(!valid){solar_os_shell_io_printf(io,"ntp: %s; RTC unchanged (%u rejected replies, %s)\n",c.stopped?"stopped":denied?"server denied request":err==ESP_OK?"timed out":"network error",rejected,esp_err_to_name(err));return;}
    if(!query)sk_clock_rtc_set(uint32_t(epoch/1000));
    solar_os_shell_io_printf(io,"ntp: %s UTC epoch=%lu from %s:%lu (RTT %lu ms, %u rejected)\n",query?"query":"RTC synchronized",(unsigned long)(epoch/1000),target,(unsigned long)port,(unsigned long)(millis()-start),rejected);
    if(!query){solar_os_datetime_t local{};solar_os_time_get_datetime(&local);solar_os_shell_io_printf(io,"local: %04u-%02u-%02u %02u:%02u:%02u\n",local.year,local.month,local.day,local.hour,local.minute,local.second);}
}
#endif
