#if SK_ETHERNET
#include <arduino_freertos.h>
#include <queue.h>
#include <semphr.h>
#include <lwip/udp.h>
#include <lwip/pbuf.h>
extern "C" {
#include "solar_os_memory.h"
}
#include <QNEthernet.h>
#if SK_NET_DIAGNOSTICS
#include <qnethernet/QNPing.h>
#include <new>
#endif
#include <qnethernet/QNDNSClient.h>
#if SK_SSH || SK_NET_DIAGNOSTICS
extern "C" void qnethernet_hal_init_entropy();
extern "C" size_t qnethernet_hal_entropy_available();
extern "C" size_t qnethernet_hal_fill_entropy(void *,size_t);
#endif
#include <cerrno>
#include <cstring>
#include "network_socket.h"
#include "qnethernet/entropy/entropy.h"
using namespace qindesign::network;
#if SK_NET_DIAGNOSTICS
struct PingProbe {
    Ping *client=nullptr; uint32_t token=0,started=0,elapsed=0;
    IPAddress ip; uint8_t payload[16]={},ttl=0; bool received=false; int error=0;
};
static PingProbe probe;
static uint32_t next_ping_token;
static void ping_reset() {delete probe.client;probe.client=nullptr;if(probe.token)probe.error=ENETDOWN;}
static void ping_reply(const PingData &reply) {
    if(!probe.token || probe.received || reply.ip!=probe.ip || reply.id!=uint16_t(probe.token>>16) ||
       reply.seq!=uint16_t(probe.token) || reply.dataSize!=sizeof(probe.payload) || !reply.data ||
       memcmp(reply.data,probe.payload,sizeof(probe.payload)))return;
    probe.elapsed=micros()-probe.started;probe.ttl=reply.ttl;probe.received=true;
}
static void ping_rpc(const sk_net_request &q,sk_net_reply &r,bool ready) {
    if(q.op==SK_NET_PING_START) {
        if(!ready){r.error=ENETDOWN;return;}
        if(probe.token){r.error=EBUSY;return;}
        if(next_ping_token>=INT32_MAX){r.error=EOVERFLOW;return;}
        probe=PingProbe{};probe.token=++next_ping_token;probe.ip=IPAddress(q.ip);
        memcpy(probe.payload,q.data,sizeof(probe.payload));
        probe.client=new(std::nothrow) Ping(ping_reply);
        if(!probe.client){probe.token=0;r.error=ENOMEM;return;}
        PingData data;data.ip=probe.ip;data.id=uint16_t(probe.token>>16);data.seq=uint16_t(probe.token);
        data.data=probe.payload;data.dataSize=sizeof(probe.payload);probe.started=micros();
        if(!probe.client->send(data)){r.error=errno?errno:EIO;delete probe.client;probe=PingProbe{};return;}
        r.value=probe.token;return;
    }
    if(!probe.token || uint32_t(q.handle)!=probe.token){r.error=EBADF;return;}
    if(q.op==SK_NET_PING_CLOSE){delete probe.client;probe=PingProbe{};return;}
    if(probe.error || !ready){r.error=probe.error?probe.error:ENETDOWN;return;}
    if(!probe.received){r.error=EAGAIN;return;}
    r.value=probe.elapsed;r.data[0]=probe.ttl;
}
#endif
static QueueHandle_t requests, replies;
static StaticQueue_t request_control, reply_control;
DMAMEM static uint8_t request_storage[sizeof(sk_net_request)], reply_storage[sizeof(sk_net_reply)];
struct Slot {
    EthernetClient client; int handle=0, failure=0; bool started=false, service=false;
    udp_pcb *udp=nullptr; uint8_t *rx=nullptr, *tx=nullptr;
    bool packet=false, rx_held=false; size_t rx_len=0, tx_len=0, tx_expected=0;
    uint8_t peer[4], target[4]; uint16_t peer_port=0, target_port=0;
};
static Slot slots[12]; // Eight OS channels plus four legacy Python socket objects.
static StaticSemaphore_t rpc_mutex_storage;
static SemaphoreHandle_t rpc_mutex;
static void udp_receive(void *arg,udp_pcb *,pbuf *p,const ip_addr_t *ip,u16_t port) {
    Slot *s=static_cast<Slot *>(arg);
    if (p && !s->packet && !s->rx_held && p->tot_len<=65507) {
        s->rx_len=p->tot_len;
        pbuf_copy_partial(p,s->rx,s->rx_len,0);
        IPAddress peer(ip4_addr_get_u32(ip_2_ip4(ip)));
        for(unsigned i=0;i<4;++i) s->peer[i]=peer[i];
        s->peer_port=port; s->packet=true;
    }
    if (p) pbuf_free(p);
}
static void release_slot(Slot &s,bool abort) {
    if (s.udp) { udp_remove(s.udp); s.udp=nullptr; }
    solar_os_memory_free(s.rx); solar_os_memory_free(s.tx); s.rx=s.tx=nullptr;
    if (abort || s.client.connecting()) s.client.abort(); else s.client.close();
    s.handle=0; s.packet=s.rx_held=false;
}
#if SK_TELNETD
static EthernetServer telnet_server(23);
static uint16_t listen_port;
static bool listening;
static int accepted_handle;
#endif
#if SK_FTP
struct FtpListener {
    EthernetServer server{0};
    EthernetClient pending;
    int handle=0;
    bool failed=false;
};
static FtpListener ftp_listeners[2];
static unsigned ftp_generation;
#endif
static unsigned next_handle;
static bool was_ready;
static uint32_t dns_generation;
static int dns_error;
static uint8_t dns_ip[4];
extern "C" void sk_net_transport_begin() {
    requests=xQueueCreateStatic(1,sizeof(sk_net_request),request_storage,&request_control);
    replies=xQueueCreateStatic(1,sizeof(sk_net_reply),reply_storage,&reply_control);
    rpc_mutex=xSemaphoreCreateMutexStatic(&rpc_mutex_storage);
    configASSERT(requests && replies && rpc_mutex);
}
extern "C" int sk_net_call(sk_net_request *request, sk_net_reply *reply) {
    static uint32_t sequence;
    if (!rpc_mutex) return ENETDOWN;
    // Every worker operation is nonblocking with respect to the network. Once
    // queued, wait for its acknowledgment: abandoning an OPEN/CLOSE on a local
    // scheduling timeout would leak a channel or leave ownership ambiguous.
    xSemaphoreTake(rpc_mutex,portMAX_DELAY);
    request->id=++sequence;
    xQueueSend(requests,request,portMAX_DELAY);
    do { xQueueReceive(replies,reply,portMAX_DELAY); } while (reply->id!=request->id);
    const int result=reply->error;
    xSemaphoreGive(rpc_mutex);
    return result;
}
extern "C" void sk_net_transport_reset() {
#if SK_NET_DIAGNOSTICS
    ping_reset();
#endif
#if SK_TELNETD
    telnet_server.end(); listening=false;
#endif
#if SK_FTP
    for(auto &listener:ftp_listeners)if(listener.handle) {
        listener.pending.abort(); listener.server.end(); listener.failed=true;
    }
#endif
    ++dns_generation; dns_error=ENETDOWN;
    for (auto &s:slots) if (s.handle) {
        s.failure=ENETDOWN;
        if (s.udp) s.packet=false;
        else s.client.abort();
    }
}
extern "C" void sk_net_transport_poll(int ready) {
    if (was_ready && !ready) sk_net_transport_reset();
    was_ready=ready;
#if SK_TELNETD
    if(ready && listen_port && !listening)listening=telnet_server.beginWithReuse(listen_port);
    if(accepted_handle) {
        bool retained=false;
        for(auto &slot:slots)if(slot.handle==accepted_handle)retained=true;
        if(!retained)accepted_handle=0;
    }
    if(listening && accepted_handle) {
        auto extra=telnet_server.accept();
        if(extra) { const char msg[]="Telnet busy; one client allowed.\r\n"; extra.write((const uint8_t *)msg,sizeof(msg)-1); extra.close(); }
    }
#endif
    sk_net_request q;
    if (xQueueReceive(requests,&q,0)!=pdTRUE) return;
    sk_net_reply r{}; r.id=q.id;
    Slot *s=nullptr;
    for (auto &slot:slots) if (slot.handle==q.handle && q.handle>0) s=&slot;
    switch(q.op) {
#if SK_FTP
    case SK_NET_LOCAL_ADDR: {
        auto ip=Ethernet.localIP();
        for(unsigned i=0;i<4;++i)r.ip[i]=ip[i];
        break;
    }
    case SK_NET_FTP_LISTEN: {
        if(!ready){r.error=ENETDOWN;break;}
        if(q.port) {
#if SK_TELNETD
            if(q.port==listen_port){r.error=EADDRINUSE;break;}
#endif
            for(auto &listener:ftp_listeners)
                if(listener.handle && listener.server.port()==q.port)r.error=EADDRINUSE;
            if(r.error)break;
        }
        r.error=EMFILE;
        for(auto &listener:ftp_listeners)if(!listener.handle) {
            if(!listener.server.beginWithReuse(q.port)){r.error=EADDRINUSE;break;}
            listener.handle=int((++ftp_generation & 0x3fffffff)+1);
            listener.failed=false;r.value=listener.handle;r.port=listener.server.port();r.error=0;break;
        }
        break;
    }
    case SK_NET_FTP_END: case SK_NET_FTP_POLL: case SK_NET_FTP_ACCEPT: {
        FtpListener *listener=nullptr;
        for(auto &entry:ftp_listeners)if(entry.handle==q.handle && q.handle)listener=&entry;
        if(!listener){r.error=EBADF;break;}
        if(q.op==SK_NET_FTP_END) {
            listener->pending.abort();listener->server.end();listener->handle=0;break;
        }
        if(!ready || listener->failed){r.error=ENETDOWN;break;}
        if(!listener->pending)listener->pending=listener->server.accept();
        if(q.op==SK_NET_FTP_POLL){r.value=bool(listener->pending);break;}
        if(!listener->pending){r.error=EAGAIN;break;}
        Slot *free_slot=nullptr;
        for(auto &entry:slots)if(!entry.handle){free_slot=&entry;break;}
        if(!free_slot){r.error=EMFILE;break;}
        auto &entry=*free_slot;
        entry.client=listener->pending;listener->pending=EthernetClient();
        entry.handle=int((++next_handle & 0x3fffffff)+1);
        entry.failure=0;entry.started=entry.service=true;
        entry.packet=entry.rx_held=false;
        r.value=entry.handle;r.port=entry.client.remotePort();
        auto ip=entry.client.remoteIP();for(unsigned i=0;i<4;++i)r.ip[i]=ip[i];
        break;
    }
#endif
#if SK_NET_DIAGNOSTICS
    case SK_NET_PING_START: case SK_NET_PING_POLL: case SK_NET_PING_CLOSE:
        ping_rpc(q,r,ready);break;
#endif
#if SK_TELNETD
    case SK_NET_LISTEN_START:
        if(!ready) { r.error=ENETDOWN; break; }
        if(listen_port) { r.error=EALREADY; break; }
#if SK_FTP
        for(auto &listener:ftp_listeners)
            if(listener.handle && listener.server.port()==q.port)r.error=EADDRINUSE;
        if(r.error)break;
#endif
        if(!q.port || !telnet_server.beginWithReuse(q.port)) { r.error=errno?errno:EADDRINUSE; break; }
        listening=true; listen_port=q.port; break;
    case SK_NET_LISTEN_STOP:
        listen_port=0; listening=false; telnet_server.end();
        for(auto &slot:slots)if(slot.handle==accepted_handle && accepted_handle)release_slot(slot,true);
        accepted_handle=0; break;
    case SK_NET_LISTEN_ACCEPT: {
        if(!ready || !listening) { r.error=ENETDOWN; break; }
        if(accepted_handle) { r.error=EBUSY; break; }
        auto client=telnet_server.accept();
        if(!client) { r.error=EAGAIN; break; }
        Slot *free_slot=nullptr;
        for(auto &slot:slots)if(!slot.handle) { free_slot=&slot; break; }
        if(!free_slot) { client.abort(); r.error=EMFILE; break; }
        auto &slot=*free_slot;
        slot.client=client; slot.handle=int((++next_handle & 0x3fffffff)+1);
        slot.failure=0; slot.started=slot.service=true;
        slot.packet=slot.rx_held=false;
        accepted_handle=r.value=slot.handle;
        auto ip=client.remoteIP(); for(unsigned i=0;i<4;++i)r.ip[i]=ip[i];
        break;
    }
#endif
#if SK_SSH || SK_NET_DIAGNOSTICS
    case SK_NET_ENTROPY: {
        // Keys can be generated before network up (or after network down).
        // Initialize once, or after the peripheral clock was disabled. Calling
        // the HAL initializer on every poll can discard a completed sample:
        // its stopped-oscillator test is also true when entropy is ready.
        static bool entropy_initialized=false;
        if (!entropy_initialized || !(CCM_CCGR6 & CCM_CCGR6_TRNG(CCM_CCGR_ON))) {
            qindesign::entropy::trng_init();
            entropy_initialized=true;
        }
        if (TRNG_MCTL & TRNG_MCTL_ERR) {
            // The HAL clears the latched TRNG error and restarts generation.
            // A zero-length read performs recovery without returning entropy
            // from the failed sample. Let the caller retry within its deadline.
            qnethernet_hal_fill_entropy(nullptr,0);
            break;
        }
        size_t n=qnethernet_hal_entropy_available();
        if (q.length<0 || q.length>SK_NET_CHUNK) { r.error=EINVAL; break; }
        if (n>size_t(q.length)) n=q.length;
        if (n) {
            r.value=qnethernet_hal_fill_entropy(r.data,n);
            if (size_t(r.value)!=n) r.error=EIO;
        }
        break;
    }
#endif
    case SK_NET_OPEN:
    case SK_NET_SERVICE_OPEN: {
        unsigned legacy=0;
        for (auto &slot:slots) if(slot.handle && !slot.service) ++legacy;
        r.error=EMFILE;
        if (q.op==SK_NET_OPEN && legacy>=4) break;
        for (auto &slot:slots) if (!slot.handle) {
            slot.handle=int((++next_handle & 0x3fffffff) + 1);
            slot.failure=0; slot.started=false; slot.packet=slot.rx_held=false;
            slot.service=q.op==SK_NET_SERVICE_OPEN;
            slot.client.setConnectionTimeoutEnabled(false);
            r.value=slot.handle; r.error=0;
            if (slot.service && q.length) {
                slot.rx=static_cast<uint8_t *>(solar_os_memory_alloc(65507,SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"net.udp.rx"));
                slot.tx=static_cast<uint8_t *>(solar_os_memory_alloc(65507,SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"net.udp.tx"));
                slot.udp=udp_new();
                if (!slot.rx || !slot.tx || !slot.udp) r.error=ENOMEM;
                else if (udp_bind(slot.udp,IP_ADDR_ANY,q.port)!=ERR_OK) r.error=EADDRINUSE;
                else udp_recv(slot.udp,udp_receive,&slot);
                if (r.error) release_slot(slot,true);
            }
            break;
        }
        break;
    }
    case SK_NET_CLOSE_ALL:
        for (auto &slot:slots) if (slot.handle && !slot.service) release_slot(slot,true);
        break;
    case SK_NET_CLOSE:
        if (s) release_slot(*s,false);
        break;
    case SK_NET_DNS_START: {
        if (!ready) { r.error=ENETDOWN; break; }
        const uint32_t generation=++dns_generation;
        dns_error=EAGAIN;
        IPAddress literal;
        if (literal.fromString(q.host)) {
            for (unsigned i=0;i<4;++i) dns_ip[i]=literal[i];
            dns_error=0;
        } else if (!DNSClient::getHostByName(q.host,[generation](const ip_addr_t *ip) {
            if (generation!=dns_generation) return;
            dns_error=ip ? 0 : EHOSTUNREACH;
            if (ip) {
                IPAddress address(ip4_addr_get_u32(ip_2_ip4(ip)));
                for (unsigned i=0;i<4;++i) dns_ip[i]=address[i];
            }
        },5000)) dns_error=EHOSTUNREACH;
        r.value=generation; break;
    }
    case SK_NET_DNS_POLL:
        r.error=uint32_t(q.handle)==dns_generation ? dns_error : ECANCELED;
        memcpy(r.ip,dns_ip,4); break;
    default:
        if (!s) { r.error=EBADF; break; }
        if (s->failure) { r.error=s->failure; break; }
        if (!ready) { r.error=ENETDOWN; break; }
        switch(q.op) {
        case SK_NET_POLL:
            if (s->udp) r.value=q.length ? 1 : s->packet;
            else if (s->client.connecting()) r.value=0;
            else if (q.length) {
                if (!s->client.connected()) r.error=ECONNREFUSED;
                else r.value=s->client.availableForWrite()>0;
            } else r.value=s->client.available()>0 || !s->client.connected();
            break;
        case SK_NET_UDP_TX_BEGIN:
            if (!s->udp || q.length<0 || q.length>65507) { r.error=EINVAL; break; }
            s->tx_len=q.length; s->tx_expected=q.length; r.owned_buffer=reinterpret_cast<uintptr_t>(s->tx); memcpy(s->target,q.ip,4); s->target_port=q.port;
            break;
        case SK_NET_UDP_TX_END: {
            if (!s->udp || s->tx_len!=s->tx_expected) { r.error=EINVAL; break; }
            pbuf *p=pbuf_alloc(PBUF_TRANSPORT,s->tx_len,PBUF_RAM);
            if (!p) { r.error=ENOMEM; break; }
            pbuf_take(p,s->tx,s->tx_len);
            ip_addr_t ip; IP_ADDR4(&ip,s->target[0],s->target[1],s->target[2],s->target[3]);
            err_t err=udp_sendto(s->udp,p,&ip,s->target_port); pbuf_free(p);
            if (err!=ERR_OK) r.error=err==ERR_MEM ? ENOMEM : EIO;
            break;
        }
        case SK_NET_UDP_RX_BEGIN:
            if (!s->udp) { r.error=EINVAL; break; }
            if (!s->packet) { r.error=EAGAIN; break; }
            s->rx_held=true; r.owned_buffer=reinterpret_cast<uintptr_t>(s->rx); r.value=s->rx_len; memcpy(r.ip,s->peer,4); r.port=s->peer_port; break;
        case SK_NET_UDP_RX_END:
            if (!s->udp) { r.error=EINVAL; break; }
            s->packet=s->rx_held=false; break;
        case SK_NET_CONNECT:
            if (s->started) { r.error=EISCONN; break; }
            s->started=true;
            if (!s->client.connect(IPAddress(q.ip),q.port)) r.error=errno ? errno : ECONNREFUSED;
            break;
        case SK_NET_CONNECTED:
            r.error=s->client.connecting() ? EAGAIN : (s->client.connected() ? 0 : ECONNREFUSED);
            break;
        case SK_NET_SEND: {
            if (!s->started) { r.error=ENOTCONN; break; }
            if (!s->client.connected()) { r.error=EPIPE; break; }
            int space=s->client.availableForWrite();
            if (space<=0) { r.error=EAGAIN; break; }
            size_t n=std::min(size_t(space),size_t(q.length));
            r.value=s->client.write(q.data,n);
            if (!r.value) r.error=EAGAIN;
            break;
        }
        case SK_NET_RECV: {
            if (!s->started) { r.error=ENOTCONN; break; }
            int available=s->client.available();
            if (available>0) {
                r.value=s->client.read(r.data,std::min(available,q.length));
                if (r.value<0) r.error=ECONNRESET;
            }
            else if (s->client.connected() || s->client.connecting()) r.error=EAGAIN;
            // A closed peer with no buffered bytes is EOF (value=0).
            break;
        }
        default: r.error=EINVAL;
        }
    }
    xQueueOverwrite(replies,&r);
}
#endif
