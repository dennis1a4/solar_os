#if SK_ETHERNET
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "solar_os_net.h"
#include "solar_os_net_transport.h"
#include "network_socket.h"
static int rpc(sk_net_request *q,sk_net_reply *r) {
    int err=sk_net_call(q,r); if (err) { errno=err; return -1; } return 0;
}
static bool parse_ip(const char *ip,uint8_t out[4]) {
    unsigned a,b,c,d; char extra;
    if (!ip || sscanf(ip,"%u.%u.%u.%u%c",&a,&b,&c,&d,&extra)!=4 || (a|b|c|d)>255) return false;
    out[0]=a; out[1]=b; out[2]=c; out[3]=d; return true;
}
int solar_os_net_transport_open(bool udp,uint16_t port) {
    sk_net_request q={.op=SK_NET_SERVICE_OPEN,.length=udp,.port=port}; sk_net_reply r;
    return rpc(&q,&r) ? -1 : r.value;
}
void solar_os_net_transport_close(int handle) {
    sk_net_request q={.op=SK_NET_CLOSE,.handle=handle}; sk_net_reply r; (void)rpc(&q,&r);
}
int solar_os_net_transport_connect(int handle,const char *ip,uint16_t port) {
    sk_net_request q={.op=SK_NET_CONNECT,.handle=handle,.port=port}; sk_net_reply r;
    if (!parse_ip(ip,q.ip)) { errno=EINVAL; return -1; }
    if (rpc(&q,&r)) return -1;
    errno=EINPROGRESS; return -1;
}
int solar_os_net_transport_wait(int handle,bool write,uint32_t timeout) {
    sk_net_request q={.op=SK_NET_POLL,.handle=handle,.length=write}; sk_net_reply r;
    TickType_t start=xTaskGetTickCount();
    do {
        if (rpc(&q,&r)) return -1;
        if (r.value) return 1;
        if (!timeout) break;
        vTaskDelay(1);
    } while ((xTaskGetTickCount()-start)*portTICK_PERIOD_MS<timeout);
    return 0;
}
ssize_t solar_os_net_transport_send(int handle,const void *data,size_t size) {
    sk_net_request q={.op=SK_NET_SEND,.handle=handle,.length=size>SK_NET_CHUNK ? SK_NET_CHUNK : size}; sk_net_reply r;
    memcpy(q.data,data,q.length); return rpc(&q,&r) ? -1 : r.value;
}
ssize_t solar_os_net_transport_recv(int handle,void *data,size_t size) {
    sk_net_request q={.op=SK_NET_RECV,.handle=handle,.length=size>SK_NET_CHUNK ? SK_NET_CHUNK : size}; sk_net_reply r;
    if (rpc(&q,&r)) return -1;
    memcpy(data,r.data,r.value); return r.value;
}
ssize_t solar_os_net_transport_sendto(int handle,const char *ip,uint16_t port,const void *data,size_t size) {
    sk_net_request q={.op=SK_NET_UDP_TX_BEGIN,.handle=handle,.port=port,.length=size}; sk_net_reply r;
    if (size>65507 || !parse_ip(ip,q.ip)) { errno=EINVAL; return -1; }
    if (rpc(&q,&r)) return -1;
    // Worker-owned staging storage remains allocated until this session closes
    // the channel. The shared service disallows simultaneous use of a session.
    if (size) memcpy((void *)r.owned_buffer,data,size);
    q.op=SK_NET_UDP_TX_END;
    return rpc(&q,&r) ? -1 : (ssize_t)size;
}
ssize_t solar_os_net_transport_recvfrom(int handle,void *data,size_t size,char *address,size_t address_size,uint16_t *port) {
    sk_net_request q={.op=SK_NET_UDP_RX_BEGIN,.handle=handle}; sk_net_reply r;
    if (rpc(&q,&r)) return -1;
    size_t total=r.value, n=total<size ? total : size;
    if (n) memcpy(data,(void *)r.owned_buffer,n);
    snprintf(address,address_size,"%u.%u.%u.%u",r.ip[0],r.ip[1],r.ip[2],r.ip[3]); *port=r.port;
    q.op=SK_NET_UDP_RX_END;
    return rpc(&q,&r) ? -1 : (ssize_t)total;
}
static StaticSemaphore_t dns_mutex_storage;
static SemaphoreHandle_t dns_mutex;
void sk_net_resolver_begin(void) { dns_mutex=xSemaphoreCreateMutexStatic(&dns_mutex_storage); configASSERT(dns_mutex); }
int sk_net_resolve(const char *host,uint8_t ip[4],uint32_t timeout,bool (*cancel)(void *),void *user) {
    if (!host || !*host || strlen(host)>=254) return EINVAL;
    if (parse_ip(host,ip)) return 0;
    if (!dns_mutex) return ENETDOWN;
    TickType_t start=xTaskGetTickCount();
    do {
        if (cancel && cancel(user)) return EINTR;
        if (xSemaphoreTake(dns_mutex,0)==pdTRUE) goto locked;
        vTaskDelay(1);
    } while ((xTaskGetTickCount()-start)*portTICK_PERIOD_MS<timeout);
    return ETIMEDOUT;
locked:;
    sk_net_request q={.op=SK_NET_DNS_START}; sk_net_reply r;
    strcpy(q.host,host); int err=sk_net_call(&q,&r);
    if (!err) {
        q.op=SK_NET_DNS_POLL; q.handle=r.value;
        for (;;) {
            if (cancel && cancel(user)) { err=EINTR; break; }
            err=sk_net_call(&q,&r);
            if (err!=EAGAIN) { if (!err) memcpy(ip,r.ip,4); break; }
            if ((xTaskGetTickCount()-start)*portTICK_PERIOD_MS>=timeout) { err=ETIMEDOUT; break; }
            vTaskDelay(1);
        }
    }
    xSemaphoreGive(dns_mutex); return err;
}
int solar_os_net_transport_resolve(const char *host,char *ip,size_t size,uint32_t timeout,bool (*cancel)(void *),void *user) {
    uint8_t address[4];
    int err=sk_net_resolve(host,address,timeout,cancel,user);
    if (err) { errno=err; return -1; }
    int len=snprintf(ip,size,"%u.%u.%u.%u",address[0],address[1],address[2],address[3]);
    if (len<0 || (size_t)len>=size) { errno=EINVAL; return -1; }
    return 0;
}
esp_err_t solar_os_net_resolve_host(const char *host,char *ip,size_t size) {
    if (!ip || !size || !host || !*host || strlen(host)>=SOLAR_OS_NET_HOST_MAX) return ESP_ERR_INVALID_ARG;
    uint8_t address[4]; int err=sk_net_resolve(host,address,5000,NULL,NULL);
    if (err) return err==ETIMEDOUT ? ESP_ERR_TIMEOUT : ESP_ERR_NOT_FOUND;
    int len=snprintf(ip,size,"%u.%u.%u.%u",address[0],address[1],address[2],address[3]);
    return len<0 || (size_t)len>=size ? ESP_ERR_INVALID_SIZE : ESP_OK;
}
esp_err_t solar_os_net_ping(const char *host,const solar_os_net_ping_options_t *options,
    solar_os_net_ping_event_fn_t event,void *event_user,solar_os_net_ping_stop_fn_t stop,void *stop_user,solar_os_net_ping_result_t *result) {
    (void)host; (void)options; (void)event; (void)event_user; (void)stop; (void)stop_user;
    if (result) memset(result,0,sizeof(*result));
    return ESP_ERR_NOT_SUPPORTED;
}
#endif
