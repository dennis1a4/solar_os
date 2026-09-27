/* Exercise the real shared service with a deterministic transport: failures,
 * session/global quotas, stale handles, timeout/cancel, EOF and UDP metadata. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "solar_os_net_session.h"
#include "solar_os_net_transport.h"
#include "solar_os_network.h"

static bool online = true, cancelled, fail_open, fail_connect, eof;
static int open_count, next_fd, ready = 1;
static int64_t now;
size_t strlcpy(char *d, const char *s, size_t n) {
    size_t len = strlen(s);
    if (n) { size_t copy = len < n-1 ? len : n-1; memcpy(d,s,copy); d[copy]=0; }
    return len;
}
int64_t esp_timer_get_time(void) { return now; }
bool solar_os_network_path_get_preferred(solar_os_network_path_info_t *info) {
    (void)info; return online;
}
static bool cancel(void *arg) { (void)arg; return cancelled; }
int solar_os_net_transport_resolve(const char *host, char *ip, size_t size,
    uint32_t timeout, bool (*stop)(void *), void *user) {
    (void)host; (void)timeout;
    if (stop && stop(user)) { errno=EINTR; return -1; }
    strlcpy(ip,"192.0.2.1",size); return 0;
}
int solar_os_net_transport_open(bool udp, uint16_t port) {
    (void)udp; (void)port;
    if (fail_open) { errno=ENOMEM; return -1; }
    ++open_count; return ++next_fd;
}
void solar_os_net_transport_close(int fd) { assert(fd>0 && open_count>0); --open_count; }
int solar_os_net_transport_connect(int fd, const char *ip, uint16_t port) {
    (void)fd; (void)ip; (void)port;
    errno=fail_connect ? ECONNREFUSED : EINPROGRESS; return -1;
}
int solar_os_net_transport_wait(int fd, bool write, uint32_t timeout) {
    (void)fd; (void)write; now+=(int64_t)timeout*1000; return ready;
}
ssize_t solar_os_net_transport_send(int fd, const void *data, size_t size) {
    (void)fd; (void)data; return size>3 ? 3 : size;
}
ssize_t solar_os_net_transport_recv(int fd, void *data, size_t size) {
    (void)fd; if (eof) return 0;
    memset(data,'T',size); return size;
}
ssize_t solar_os_net_transport_sendto(int fd, const char *ip, uint16_t port, const void *data, size_t size) {
    (void)fd; (void)ip; (void)port; (void)data; return size;
}
ssize_t solar_os_net_transport_recvfrom(int fd, void *data, size_t size,
    char *address, size_t address_size, uint16_t *port) {
    (void)fd; memset(data,'U',size<12 ? size : 12);
    strlcpy(address,"192.0.2.2",address_size); *port=1234; return 12;
}
int main(void) {
    solar_os_net_session_t *a, *b, *c;
    assert(solar_os_net_session_create("a",cancel,NULL,&a)==ESP_OK);
    assert(solar_os_net_session_create("b",NULL,NULL,&b)==ESP_OK);
    assert(solar_os_net_session_create("c",NULL,NULL,&c)==ESP_OK);
    uint32_t h, handles[4], other[4];
    online=false;
    assert(solar_os_net_session_udp_open(a,0,&h)==ESP_ERR_INVALID_STATE);
    online=true; fail_open=true;
    assert(solar_os_net_session_udp_open(a,0,&h)==ESP_ERR_NO_MEM);
    assert(open_count==0); fail_open=false;
    fail_connect=true;
    assert(solar_os_net_session_tcp_connect(a,"host",80,100,&h)!=ESP_OK);
    assert(open_count==0); fail_connect=false;
    for (int i=0;i<4;++i) {
        assert(solar_os_net_session_udp_open(a,0,&handles[i])==ESP_OK);
        assert(solar_os_net_session_udp_open(b,0,&other[i])==ESP_OK);
    }
    assert(solar_os_net_session_udp_open(a,0,&h)==ESP_ERR_NO_MEM);
    assert(solar_os_net_session_udp_open(c,0,&h)==ESP_ERR_NO_MEM);
    solar_os_net_session_status_t status;
    solar_os_net_session_get_status(a,&status);
    assert(status.open_channels==4 && status.global_open_channels==8);
    assert(solar_os_net_session_close(a,handles[0])==ESP_OK);
    assert(solar_os_net_session_udp_open(a,0,&h)==ESP_OK && h!=handles[0]);
    assert(solar_os_net_session_close(a,handles[0])==ESP_ERR_INVALID_ARG);
    solar_os_net_session_destroy(a); assert(open_count==4);
    solar_os_net_session_get_status(b,&status);
    assert(status.open_channels==4 && status.global_open_channels==4);
    solar_os_net_session_close_all(b); assert(open_count==0);
    assert(solar_os_net_session_tcp_connect(b,"host",80,100,&h)==ESP_OK);
    assert(solar_os_net_session_tcp_send(b,h,"partial writes",14,1000)==ESP_OK);
    char data[5]; solar_os_net_receive_result_t result;
    ready=0;
    assert(solar_os_net_session_tcp_receive(b,h,data,sizeof(data),100,&result)==ESP_OK && result.timed_out);
    ready=1; eof=true;
    assert(solar_os_net_session_tcp_receive(b,h,data,sizeof(data),100,&result)==ESP_OK && result.closed);
    assert(solar_os_net_session_close(b,h)==ESP_OK);
    assert(solar_os_net_session_udp_open(b,0,&h)==ESP_OK);
    assert(solar_os_net_session_udp_receive(b,h,data,sizeof(data),100,&result)==ESP_OK);
    assert(result.data_len==5 && result.message_len==12 && result.truncated);
    assert(result.port==1234 && !strcmp(result.address,"192.0.2.2"));
    assert(solar_os_net_session_udp_send(b,h,"host",1234,NULL,0,100)==ESP_OK);
    assert(solar_os_net_session_websocket_connect(b,"ws://host",NULL,100,&h)==ESP_ERR_NOT_SUPPORTED);
    solar_os_net_session_destroy(b); solar_os_net_session_destroy(c);
    assert(open_count==0);
    assert(solar_os_net_session_create("cancel",cancel,NULL,&a)==ESP_OK);
    cancelled=true;
    assert(solar_os_net_session_tcp_connect(a,"host",80,100,&h)==ESP_ERR_INVALID_STATE);
    solar_os_net_session_destroy(a);
    puts("PASS: shared network session contracts");
}
