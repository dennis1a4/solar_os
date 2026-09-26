#if SK_ETHERNET
#include <arduino_freertos.h>
#include <queue.h>
#include <QNEthernet.h>
#include <qnethernet/QNDNSClient.h>
#include <cerrno>
#include <cstring>
#include "network_socket.h"
using namespace qindesign::network;
static QueueHandle_t requests, replies;
static StaticQueue_t request_control, reply_control;
DMAMEM static uint8_t request_storage[sizeof(sk_net_request)], reply_storage[sizeof(sk_net_reply)];
struct Slot { EthernetClient client; int handle=0, failure=0; bool started=false; };
static Slot slots[4];
static unsigned next_handle;
static bool was_ready;
static uint32_t dns_generation;
static int dns_error;
static uint8_t dns_ip[4];
extern "C" void sk_net_transport_begin() {
    requests=xQueueCreateStatic(1,sizeof(sk_net_request),request_storage,&request_control);
    replies=xQueueCreateStatic(1,sizeof(sk_net_reply),reply_storage,&reply_control);
    configASSERT(requests && replies);
}
extern "C" int sk_net_call(sk_net_request *request, sk_net_reply *reply) {
    static uint32_t sequence;
    request->id=++sequence;
    if (!requests || xQueueSend(requests,request,0)!=pdTRUE) return EBUSY;
    TickType_t start=xTaskGetTickCount();
    while (xTaskGetTickCount()-start<pdMS_TO_TICKS(1000)) {
        if (xQueueReceive(replies,reply,pdMS_TO_TICKS(10))==pdTRUE && reply->id==request->id)
            return reply->error;
    }
    return ETIMEDOUT;
}
extern "C" void sk_net_transport_reset() {
    ++dns_generation; dns_error=ENETDOWN;
    for (auto &s:slots) if (s.handle) { s.client.abort(); s.failure=ENETDOWN; }
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
    case SK_NET_OPEN:
        r.error=EMFILE;
        for (auto &slot:slots) if (!slot.handle) {
            slot.handle=int((++next_handle & 0x3fffffff) + 1);
            slot.failure=0; slot.started=false;
            slot.client.setConnectionTimeoutEnabled(false);
            r.value=slot.handle; r.error=0; break;
        }
        break;
    case SK_NET_CLOSE_ALL:
        ++dns_generation;
        for (auto &slot:slots) { slot.client.abort(); slot.handle=0; }
        break;
    case SK_NET_CLOSE:
        if (s) {
            if (s->client.connecting()) s->client.abort();
            else s->client.close();
            s->handle=0;
        }
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
