#if SK_ETHERNET
#include <arduino_freertos.h>
#include <queue.h>
#include <QNEthernet.h>

#include <cstdlib>
#include "network_socket.h"
#include <cstring>
extern "C" {
#include "solar_os_shell_commands.h"
#include "solar_os_shell_io.h"
#include "solar_os_net_session.h"
}
using namespace qindesign::network;
// QNEthernet is not thread-safe. Only this task may touch its APIs; automatic
// yield polling is disabled in the profile. Shell requests/replies are copied.
enum class Operation { Status, Up, Down, Resolve, Connect };
struct Request { Operation op; uint32_t id; char host[254]; uint16_t port; };
struct Reply { uint32_t id; char text[384]; };
static QueueHandle_t requests, replies;
static StaticQueue_t request_control, reply_control;
static uint8_t request_storage[sizeof(Request)], reply_storage[sizeof(Reply)];
DMAMEM static StackType_t network_stack[2048];
static StaticTask_t network_task_control;
static bool running;
static void address(char *out, size_t size, const IPAddress &ip) {
    snprintf(out, size, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}
static void status(Reply &reply) {
    char ip[16], mask[16], gateway[16], dns[16];
    address(ip,sizeof(ip),Ethernet.localIP()); address(mask,sizeof(mask),Ethernet.subnetMask());
    address(gateway,sizeof(gateway),Ethernet.gatewayIP()); address(dns,sizeof(dns),Ethernet.dnsServerIP());
    uint8_t mac[6]; Ethernet.macAddress(mac);
    snprintf(reply.text,sizeof(reply.text),
        "eth0: %s link=%s address=%s mask=%s gateway=%s dns=%s\n"
        "MAC=%02x:%02x:%02x:%02x:%02x:%02x DHCP=%s task-stack-free=%u words\n",
        running ? "started" : "stopped", Ethernet.linkState() ? "up" : "down",
        ip,mask,gateway,dns,mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],
        running ? (uint32_t(Ethernet.localIP()) ? "bound" : "waiting") : "off",
        unsigned(uxTaskGetStackHighWaterMark(nullptr)));
}
void sk_network_registry_begin();
void sk_network_registry_poll(bool started);
static void network_task(void *) {
    for (;;) {
        if (running) Ethernet.loop();
        sk_network_registry_poll(running);
        sk_net_transport_poll(running && Ethernet.linkState() && uint32_t(Ethernet.localIP()));
        Request request;
        if (xQueueReceive(requests,&request,0)==pdTRUE) {
            Reply reply{}; reply.id=request.id;
            if (request.op==Operation::Up) {
                if (!running) running=Ethernet.begin();
                if (!running) snprintf(reply.text,sizeof(reply.text),"Ethernet start failed (check fitted PHY)\n");
                else status(reply);
            } else if (request.op==Operation::Down) {
                sk_net_transport_reset();
                Ethernet.end(); running=false; status(reply);
            } else if (request.op==Operation::Status) status(reply);
            xQueueOverwrite(replies,&reply);
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
void sk_network_begin() {
    sk_network_registry_begin();
    sk_net_transport_begin();
    sk_net_resolver_begin();
    requests=xQueueCreateStatic(1,sizeof(Request),request_storage,&request_control);
    replies=xQueueCreateStatic(1,sizeof(Reply),reply_storage,&reply_control);
    configASSERT(requests && replies);
    configASSERT(xTaskCreateStatic(network_task,"ethernet",2048,nullptr,2,
        network_stack,&network_task_control));
}
static void ethernet_command(solar_os_context_t *ctx,int argc,char **argv) {
    auto *io=solar_os_context_shell_io(ctx);
    static uint32_t sequence;
    Request request{}; request.id=++sequence;
    bool valid=true;
    if (argc==1 || (argc==2 && (!strcmp(argv[1],"status") || !strcmp(argv[1],"interfaces")))) request.op=Operation::Status;
    else if (argc==2 && !strcmp(argv[1],"up")) request.op=Operation::Up;
    else if (argc==2 && !strcmp(argv[1],"down")) request.op=Operation::Down;
    else if ((argc==3 && !strcmp(argv[1],"resolve")) || (argc==4 && !strcmp(argv[1],"connect"))) {
        request.op=argc==3 ? Operation::Resolve : Operation::Connect;
        valid=argv[2][0] && strlen(argv[2])<sizeof(request.host);
        if (valid) strcpy(request.host,argv[2]);
        if (argc==4) {
            char *end; unsigned long port=strtoul(argv[3],&end,10);
            valid=valid && argv[3][0] && !*end && port>0 && port<=65535;
            request.port=port;
        }
    } else valid=false;
    if (!valid) {
        solar_os_shell_io_writeln(io,"usage: network [status|interfaces|up|down|resolve HOST|connect HOST PORT]"); return;
    }
    if (request.op==Operation::Resolve || request.op==Operation::Connect) {
        char ip[SOLAR_OS_NET_ADDR_MAX];
        esp_err_t err=solar_os_net_resolve_host(request.host,ip,sizeof(ip));
        if (err!=ESP_OK) { solar_os_shell_io_writeln(io,"DNS lookup failed or network unavailable"); return; }
        if (request.op==Operation::Resolve) { solar_os_shell_io_writeln(io,ip); return; }
        solar_os_net_session_t *session=nullptr; uint32_t handle=0;
        err=solar_os_net_session_create("network.command",nullptr,nullptr,&session);
        if (err==ESP_OK) err=solar_os_net_session_tcp_connect(session,ip,request.port,3000,&handle);
        solar_os_shell_io_printf(io,"TCP %s:%u %s\n",ip,request.port,err==ESP_OK ? "connected" : "failed");
        solar_os_net_session_destroy(session);
        return;
    }
    if (!requests || xQueueSend(requests,&request,0)!=pdTRUE) {
        solar_os_shell_io_writeln(io,"Network busy or unavailable"); return;
    }
    const TickType_t start=xTaskGetTickCount(), limit=pdMS_TO_TICKS(10000);
    Reply reply;
    while (xTaskGetTickCount()-start<limit) {
        if (xQueueReceive(replies,&reply,pdMS_TO_TICKS(20))==pdTRUE && reply.id==request.id) {
            solar_os_shell_io_write(io,reply.text); return;
        }
    }
    solar_os_shell_io_writeln(io,"Network request timed out");
}
// Preserve the bring-up controls while the shared command owns OS status/routes.
extern "C" bool solar_os_shell_network_transport_command(solar_os_context_t *ctx,int argc,char **argv) {
    if (argc>1 && (!strcmp(argv[1],"up") || !strcmp(argv[1],"down") ||
                  !strcmp(argv[1],"resolve") || !strcmp(argv[1],"connect"))) {
        ethernet_command(ctx,argc,argv); return true;
    }
    if (argc==1 || (argc==2 && !strcmp(argv[1],"status"))) ethernet_command(ctx,argc,argv);
    return false;
}
#endif
