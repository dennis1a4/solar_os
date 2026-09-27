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
#include <qnethernet/QNDNSClient.h>
#if SK_SSH
extern "C" size_t qnethernet_hal_entropy_available();
extern "C" size_t qnethernet_hal_fill_entropy(void *,size_t);
#endif
#include <cerrno>
#include <cstring>
#include "network_socket.h"
using namespace qindesign::network;
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
    sk_net_request q;
    if (xQueueReceive(requests,&q,0)!=pdTRUE) return;
    sk_net_reply r{}; r.id=q.id;
    Slot *s=nullptr;
    for (auto &slot:slots) if (slot.handle==q.handle && q.handle>0) s=&slot;
    switch(q.op) {
#if SK_SSH
    case SK_NET_ENTROPY: {
        // Ethernet and SSH share one TRNG owner. Never reinitialize it from
        // the SSH task, or race the Ethernet stack's entropy pool.
        if (TRNG_MCTL & TRNG_MCTL_ERR) { r.error=EIO; break; }
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
