#if SK_ETHERNET
#include <arduino_freertos.h>
#include <queue.h>
#include <semphr.h>
#include <QNEthernet.h>
#include <qnethernet/lwip_driver.h>
#if SK_WIRELESS_BRIDGE
#include "wifi_network.h"
#include <lwip/dns.h>
#endif
extern "C" {
#include "solar_os_network.h"
}
using namespace qindesign::network;
struct sk_netif { esp_netif_ip_info_t ip; esp_netif_dns_info_t dns; struct netif *impl; int priority; };
static sk_netif ethernet_netif;
#if SK_WIRELESS_BRIDGE
static sk_netif wifi_netif;
static bool wifi_registered, wifi_last_ready, wifi_last_connecting;
#endif
static bool registered, last_ready, last_connecting;
extern "C" esp_netif_t *esp_netif_get_default_netif() {
#if SK_WIRELESS_BRIDGE
return netif_default && netif_default==wifi_netif.impl?&wifi_netif:&ethernet_netif;
#else
return &ethernet_netif;
#endif
 }
extern "C" void *esp_netif_get_netif_impl(esp_netif_t *n) { return n ? n->impl : nullptr; }
extern "C" int esp_netif_set_route_prio(esp_netif_t *n,int p) { if (!n) return -1; n->priority=p; return p; }
extern "C" esp_err_t esp_netif_get_dns_info(esp_netif_t *n,int which,esp_netif_dns_info_t *dns) {
    if (!n || !dns || which!=ESP_NETIF_DNS_MAIN) return ESP_ERR_INVALID_ARG;
    taskENTER_CRITICAL(); *dns=n->dns; taskEXIT_CRITICAL(); return ESP_OK;
}
extern "C" esp_err_t esp_netif_get_ip_info(esp_netif_t *n,esp_netif_ip_info_t *ip) {
    if (!n || !ip) return ESP_ERR_INVALID_ARG;
    taskENTER_CRITICAL(); *ip=n->ip; taskEXIT_CRITICAL(); return ESP_OK;
}
extern "C" char *esp_ip4addr_ntoa(const esp_ip4_addr_t *ip,char *buffer,int size) {
    if (!ip || !buffer || size<=0) return nullptr;
    const auto *b=reinterpret_cast<const uint8_t *>(&ip->addr);
    int n=snprintf(buffer,size,"%u.%u.%u.%u",b[0],b[1],b[2],b[3]);
    return n>=0 && n<size ? buffer : nullptr;
}
extern "C" int64_t esp_timer_get_time() {
    TimeOut_t now; vTaskSetTimeOutState(&now);
    const uint64_t ticks=(uint64_t(uint32_t(now.xOverflowCount)) << 32) | now.xTimeOnEntering;
    return int64_t(ticks)*portTICK_PERIOD_MS*1000;
}
void sk_network_registry_poll(bool started) {
    sk_netif state{};
    state.ip.ip.addr=uint32_t(Ethernet.localIP());
    state.ip.netmask.addr=uint32_t(Ethernet.subnetMask());
    state.ip.gw.addr=uint32_t(Ethernet.gatewayIP());
    state.dns.ip.u_addr.ip4.addr=uint32_t(Ethernet.dnsServerIP());
#if SK_WIRELESS_BRIDGE
    // lwIP's DNS table is global; retain each path's DHCP-provided resolver.
    if(state.ip.ip.addr && state.ip.ip.addr==ethernet_netif.ip.ip.addr &&
       ethernet_netif.dns.ip.u_addr.ip4.addr)
        state.dns=ethernet_netif.dns;
#endif
    state.impl=started ? qindesign::network::enet::netif() : nullptr;
    const bool ready=started && Ethernet.linkState() && state.ip.ip.addr;
    const bool connecting=started && !ready;
    const bool changed=memcmp(&state.ip,&ethernet_netif.ip,sizeof(state.ip)) ||
        memcmp(&state.dns,&ethernet_netif.dns,sizeof(state.dns));
    taskENTER_CRITICAL();
    ethernet_netif.ip=state.ip; ethernet_netif.dns=state.dns; ethernet_netif.impl=state.impl;
    taskEXIT_CRITICAL();
    if (!registered) {
        registered=solar_os_network_path_register("eth0",&ethernet_netif,10)==ESP_OK;
        configASSERT(registered);
    }
    if (changed || ready!=last_ready || connecting!=last_connecting) {
        solar_os_network_path_set_ready(&ethernet_netif,ready);
        if (connecting) solar_os_network_path_set_connecting(&ethernet_netif,true);
        last_ready=ready; last_connecting=connecting;
    }
#if SK_WIRELESS_BRIDGE
    struct netif *wn=sk_wifi_netif();
    if(wn) {
        sk_netif w{}; w.impl=wn;
        w.ip.ip.addr=ip4_addr_get_u32(netif_ip4_addr(wn));
        w.ip.netmask.addr=ip4_addr_get_u32(netif_ip4_netmask(wn));
        w.ip.gw.addr=ip4_addr_get_u32(netif_ip4_gw(wn));
        w.dns.ip.u_addr.ip4.addr=sk_wifi_dns();
        bool wr=netif_is_up(wn) && netif_is_link_up(wn) && w.ip.ip.addr;
        bool wc=sk_wifi_connecting();
        bool change=memcmp(&w.ip,&wifi_netif.ip,sizeof(w.ip)) || memcmp(&w.dns,&wifi_netif.dns,sizeof(w.dns));
        taskENTER_CRITICAL();
        wifi_netif.ip=w.ip; wifi_netif.dns=w.dns; wifi_netif.impl=wn;
        taskEXIT_CRITICAL();
        if(!wifi_registered) {
            wifi_registered=solar_os_network_path_register("wifi0",&wifi_netif,5)==ESP_OK;
            configASSERT(wifi_registered);
        }
        if(change || wr!=wifi_last_ready || wc!=wifi_last_connecting) {
            solar_os_network_path_set_ready(&wifi_netif,wr);
            if(wc) solar_os_network_path_set_connecting(&wifi_netif,true);
            wifi_last_ready=wr; wifi_last_connecting=wc;
        }
    }
    // Registry preferences become the actual lwIP route on its owner task.
    struct netif *preferred=solar_os_network_lwip_preferred();
    if(netif_default!=preferred) netif_set_default(preferred);
    uint32_t resolver=preferred==wifi_netif.impl?wifi_netif.dns.ip.u_addr.ip4.addr:
        (preferred==ethernet_netif.impl?ethernet_netif.dns.ip.u_addr.ip4.addr:0);
    if(resolver && resolver!=ip4_addr_get_u32(ip_2_ip4(dns_getserver(0)))) {
        ip_addr_t address; IP_ADDR4(&address,0,0,0,0);
        ip4_addr_set_u32(ip_2_ip4(&address),resolver); dns_setserver(0,&address);
    }
#endif
}
#if SK_WIRELESS_BRIDGE
extern "C" struct netif *sk_network_route(const ip4_addr_t *src,const ip4_addr_t *dest) {
    struct netif *n;
    if(src && !ip4_addr_isany(src)) {
        NETIF_FOREACH(n) {
            if(netif_is_up(n) && netif_is_link_up(n) && ip4_addr_cmp(src,netif_ip4_addr(n))) return n;
        }
        return nullptr;
    }
    struct netif *preferred=solar_os_network_lwip_preferred();
    if(!preferred || !netif_is_up(preferred) || !netif_is_link_up(preferred)) return nullptr;
    if(ip4_addr_netcmp(dest,netif_ip4_addr(preferred),netif_ip4_netmask(preferred))) return preferred;
    NETIF_FOREACH(n) {
        if(netif_is_up(n) && netif_is_link_up(n) && !ip4_addr_isany(netif_ip4_addr(n)) &&
           ip4_addr_netcmp(dest,netif_ip4_addr(n),netif_ip4_netmask(n))) return n;
    }
    return preferred;
}
#endif
// Deliver registry events away from the lwIP owner task. Handlers can query
// services without re-entering Ethernet.loop or blocking packet processing.
struct Handler { esp_event_base_t base; int32_t id; esp_event_handler_t fn; void *arg; };
struct Event { esp_event_base_t base; int32_t id; alignas(4) uint8_t data[128]; };
static Handler handlers[8];
static StaticSemaphore_t handler_mutex_storage;
static SemaphoreHandle_t handler_mutex;
static QueueHandle_t events;
static StaticQueue_t event_control;
DMAMEM static uint8_t event_storage[4*sizeof(Event)];
DMAMEM static StackType_t event_stack[1536];
static StaticTask_t event_task_control;
static void event_task(void *) {
    Event e;
    for (;;) if (xQueueReceive(events,&e,portMAX_DELAY)==pdTRUE) {
        // Unregister waits for an in-flight callback before its caller can
        // release the callback argument. Recursive locking permits handlers to
        // unregister themselves or other handlers during dispatch.
        xSemaphoreTakeRecursive(handler_mutex,portMAX_DELAY);
        for (auto &h:handlers) if (h.fn && (!h.base || h.base==e.base) && (h.id==ESP_EVENT_ANY_ID || h.id==e.id))
            h.fn(h.arg,e.base,e.id,e.data);
        xSemaphoreGiveRecursive(handler_mutex);
    }
}
void sk_network_registry_begin() {
    handler_mutex=xSemaphoreCreateRecursiveMutexStatic(&handler_mutex_storage);
    configASSERT(handler_mutex);
    events=xQueueCreateStatic(4,sizeof(Event),event_storage,&event_control);
    configASSERT(events);
    configASSERT(xTaskCreateStatic(event_task,"net-events",1536,nullptr,1,event_stack,&event_task_control));
}
extern "C" esp_err_t esp_event_post(esp_event_base_t base,int32_t id,const void *data,size_t size,TickType_t wait) {
    if (!events) return ESP_ERR_INVALID_STATE;
    if (!base || size>sizeof(Event::data) || (size && !data)) return ESP_ERR_INVALID_ARG;
    Event e{}; e.base=base; e.id=id; if (size) memcpy(e.data,data,size);
    return xQueueSend(events,&e,wait)==pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}
extern "C" esp_err_t esp_event_handler_register(esp_event_base_t base,int32_t id,esp_event_handler_t fn,void *arg) {
    if (!fn) return ESP_ERR_INVALID_ARG;
    if (!handler_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTakeRecursive(handler_mutex,portMAX_DELAY);
    Handler *free=nullptr;
    for (auto &h:handlers) {
        if (h.fn==fn && h.base==base && h.id==id) { h.arg=arg; xSemaphoreGiveRecursive(handler_mutex); return ESP_OK; }
        if (!h.fn && !free) free=&h;
    }
    if (free) *free={base,id,fn,arg};
    xSemaphoreGiveRecursive(handler_mutex); return free ? ESP_OK : ESP_ERR_NO_MEM;
}
extern "C" esp_err_t esp_event_handler_unregister(esp_event_base_t base,int32_t id,esp_event_handler_t fn) {
    if (!handler_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTakeRecursive(handler_mutex,portMAX_DELAY);
    for (auto &h:handlers) if (h.fn==fn && h.base==base && h.id==id) h={};
    xSemaphoreGiveRecursive(handler_mutex); return ESP_OK;
}
#endif
