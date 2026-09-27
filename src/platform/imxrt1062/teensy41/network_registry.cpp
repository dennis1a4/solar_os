#if SK_ETHERNET
#include <arduino_freertos.h>
#include <queue.h>
#include <semphr.h>
#include <QNEthernet.h>
extern "C" {
#include "solar_os_network.h"
}
using namespace qindesign::network;
struct sk_netif { esp_netif_ip_info_t ip; esp_netif_dns_info_t dns; struct netif *impl; int priority; };
static sk_netif ethernet_netif;
static bool registered, last_ready, last_connecting;
extern "C" esp_netif_t *esp_netif_get_default_netif() { return &ethernet_netif; }
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
    state.impl=started ? netif_default : nullptr;
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
}
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
