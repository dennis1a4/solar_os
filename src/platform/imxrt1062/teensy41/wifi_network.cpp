#if SK_WIRELESS_BRIDGE && SK_ETHERNET
#include "wifi_network.h"
#include "wireless_bridge.h"
#include <arduino_freertos.h>
#include <QNEthernet.h>
#include <lwip/dhcp.h>
#include <lwip/dns.h>
#include <lwip/etharp.h>
#include <netif/ethernet.h>
#if LWIP_SINGLE_NETIF || !CHECKSUM_GEN_IP || !CHECKSUM_GEN_TCP || !CHECKSUM_GEN_UDP
#error Wi-Fi requires multiple lwIP interfaces and software checksums
#endif
extern "C" {
#include "solar_os_shell_io.h"
#include "solar_os_port.h"
#include "wireless/bridge_wire.h"
}
#include <atomic>
static struct netif wifi;
static bool added, linked;
static uint32_t last_link, last_rx, epoch, dns_address;
static std::atomic<bool> connecting{false};
static uint8_t link_info[52];
static sk_wifi_status_t cached_status{};
void sk_wifi_status(sk_wifi_status_t *out) {
  taskENTER_CRITICAL();
  *out=cached_status;
  taskEXIT_CRITICAL();
}
DMAMEM static uint8_t frame[1514];
static uint32_t tx_packets, rx_packets, tx_drops, rx_drops;
static err_t output(struct netif *, struct pbuf *p) {
  if(!linked || !p || p->tot_len<ETH_PAD_SIZE+14 || p->tot_len>sizeof(frame)+ETH_PAD_SIZE) return ERR_IF;
  // pbuf may be chained; bridge copies the full frame before returning.
  pbuf_copy_partial(p,frame,p->tot_len-ETH_PAD_SIZE,ETH_PAD_SIZE);
  unsigned r=sk_wireless_wifi_tx(frame,p->tot_len-ETH_PAD_SIZE);
  if(r) { tx_drops++; return ERR_IF; }
  tx_packets++; return ERR_OK;
}
static err_t initialize(struct netif *n) {
  n->name[0]='w'; n->name[1]='l'; n->mtu=1500; n->hwaddr_len=6;
  memcpy(n->hwaddr,link_info+4,6);
  n->flags=NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
  n->output=etharp_output; n->linkoutput=output;
  return ERR_OK;
}
struct netif *sk_wifi_netif() { return added?&wifi:nullptr; }
bool sk_wifi_connecting() { return connecting; }
uint32_t sk_wifi_dns() { return dns_address; }
void sk_wifi_network_poll(bool lwip_ready) {
  if(!lwip_ready) return;
  uint32_t now=millis();
  if(now-last_link>=500) {
    last_link=now; size_t n=0;
    bool ok=sk_wireless_wifi_available() &&
      sk_wireless_wifi_command(WB_WIFI_LINK,nullptr,0,link_info,sizeof(link_info),&n)==WB_OK && n==52;
    sk_wifi_status_t status{};
    status.available=ok;
    if(ok) {
      status.state=link_info[0]; status.rssi=(int8_t)link_info[19];
      for(unsigned i=0;i<32;i++) {
        const uint8_t ch=link_info[20+i];
        status.ssid[i]=ch>=32 && ch<127?ch:(ch?'?':0);
      }
    }
    taskENTER_CRITICAL();
    cached_status=status;
    taskEXIT_CRITICAL();
    bool up=ok && link_info[0]==2;
    connecting=ok && link_info[0]==1;
    if(ok && !added) {
      ip4_addr_t zero{};
      added=netif_add(&wifi,&zero,&zero,&zero,nullptr,initialize,ethernet_input)!=nullptr;
    }
    uint32_t next_epoch=ok?wb_u32(link_info+10):0;
    if(added && linked && (!up || next_epoch!=epoch)) {
      linked=false; dhcp_stop(&wifi); netif_set_link_down(&wifi); netif_set_down(&wifi);
      ip4_addr_t zero{}; netif_set_addr(&wifi,&zero,&zero,&zero); dns_address=0;
    }
    if(added && up && !linked) {
      memcpy(wifi.hwaddr,link_info+4,6); epoch=next_epoch; linked=true;
      netif_set_up(&wifi); netif_set_link_up(&wifi);
      if(dhcp_start(&wifi)!=ERR_OK) { linked=false; netif_set_link_down(&wifi); netif_set_down(&wifi); }
    }
  }
  if(!added || !linked) return;
  connecting=!ip4_addr_isany_val(*netif_ip4_addr(&wifi))?false:true;
  if(now-last_rx<5 || (!sk_wireless_wifi_pending() && now-last_rx<1000)) return;
  last_rx=now;
  // Bound work per pass so Ethernet and socket requests remain serviced.
  for(unsigned i=0;i<2;i++) {
    size_t n=0;
    unsigned r=sk_wireless_wifi_rx(frame,sizeof(frame),&n);
    if(r || !n) break;
    struct pbuf *p=pbuf_alloc(PBUF_RAW,n+ETH_PAD_SIZE,PBUF_POOL);
    if(!p) { rx_drops++; break; }
    pbuf_take_at(p,frame,n,ETH_PAD_SIZE);
    if(wifi.input(p,&wifi)!=ERR_OK) { pbuf_free(p); rx_drops++; }
    else rx_packets++;
    // Capture DHCP DNS before the registry restores the preferred path's DNS.
    if(!dns_address && !ip4_addr_isany(netif_ip4_addr(&wifi)))
      dns_address=ip4_addr_get_u32(ip_2_ip4(dns_getserver(0)));
  }
}
static void wipe(void *ptr,size_t n) { volatile uint8_t *p=(volatile uint8_t *)ptr; while(n--) *p++=0; }
extern "C" void sk_wifi_shell(solar_os_context_t *ctx,int argc,char **argv) {
  auto *io=solar_os_context_shell_io(ctx);
  io->command_status=1;
  unsigned op=0;
  uint8_t payload[98]={}; size_t length=0;
  if(argc==1 || (argc==2 && !strcmp(argv[1],"status"))) {
    uint8_t d[52]; size_t n=0;
    unsigned r=sk_wireless_wifi_command(WB_WIFI_LINK,nullptr,0,d,sizeof(d),&n);
    if(r || n!=52) { solar_os_shell_io_writeln(io,"Wi-Fi bridge unavailable; use wbridge on"); return; }
    char ssid[33]; for(unsigned i=0;i<32;i++) ssid[i]=d[20+i]>=32 && d[20+i]<127?d[20+i]:(d[20+i]?'?':0); ssid[32]=0;
    const char *names[]={"disconnected","connecting","associated","failed"};
    solar_os_shell_io_printf(io,"wifi0: %s SSID=%s RSSI=%d reason=%u\n",
      d[0]<4?names[d[0]]:"unknown",ssid,(int8_t)d[19],wb_u16(d+2));
    solar_os_shell_io_printf(io,"Frames: tx=%lu rx=%lu tx_drops=%lu rx_drops=%lu ESP_rx_drops=%lu\n",
      (unsigned long)tx_packets,(unsigned long)rx_packets,(unsigned long)tx_drops,(unsigned long)rx_drops,(unsigned long)wb_u32(d+14));
    solar_os_shell_io_writeln(io,"Use network status for IP address and route."); io->command_status=0; return;
  }
  if(argc==2) {
    if(!strcmp(argv[1],"disconnect")) op=WB_WIFI_LEAVE;
    else if(!strcmp(argv[1],"reconnect")) op=WB_WIFI_RECONNECT;
    else if(!strcmp(argv[1],"save")) op=WB_WIFI_SAVE;
    else if(!strcmp(argv[1],"forget")) op=WB_WIFI_FORGET;
  }
  if((argc==3 || argc==4) && !strcmp(argv[1],"connect")) {
    size_t ssid_len=strlen(argv[2]);
    if(!ssid_len || ssid_len>32 || (argc==4 && strcmp(argv[3],"--open"))) goto usage;
    if(!sk_wireless_wifi_available()) { solar_os_shell_io_writeln(io,"Wi-Fi bridge unavailable; use wbridge on"); return; }
    memcpy(payload+2,argv[2],ssid_len); payload[0]=ssid_len;
    if(argc==3) {
      if(io->kind!=SOLAR_OS_SHELL_IO_KIND_PORT || io->command_input) {
        solar_os_shell_io_writeln(io,"Run wifi connect from an interactive console"); return;
      }
      solar_os_shell_io_write(io,"Wi-Fi password (hidden; Ctrl-C cancels): ");
      uint32_t began=millis(); bool done=false, cancelled=false;
      while(millis()-began<120000) {
        uint8_t ch; size_t got=0;
        if(solar_os_port_read(&io->port,&ch,1,100,&got)!=ESP_OK) { cancelled=true; break; }
        if(!got) continue;
        if(ch==3 || ch==27) { cancelled=true; break; }
        if(ch=='\r' || ch=='\n') { done=true; break; }
        if(ch==8 || ch==127) { if(payload[1]) payload[1]--; }
        else if(ch>=32 && payload[1]<63) payload[34+payload[1]++]=ch;
        else { cancelled=true; break; }
      }
      solar_os_shell_io_writeln(io,"");
      if(!done || cancelled || payload[1]<8) {
        wipe(payload,sizeof(payload)); solar_os_shell_io_writeln(io,"Wi-Fi connection cancelled or invalid password length"); return;
      }
    }
    op=WB_WIFI_JOIN; length=sizeof(payload);
  }
  if(op) {
    unsigned r=sk_wireless_wifi_command(op,payload,length,nullptr,0,nullptr);
    wipe(payload,sizeof(payload));
    if(r) solar_os_shell_io_printf(io,"Wi-Fi command failed (%u); disconnect before changing networks\n",r);
    else { io->command_status=0; solar_os_shell_io_writeln(io,op==WB_WIFI_JOIN || op==WB_WIFI_RECONNECT?"Wi-Fi connecting; use wifi status and network status":"Wi-Fi: ok"); }
    return;
  }
usage:
  solar_os_shell_io_writeln(io,"wifi scan|status|connect SSID [--open]|disconnect|reconnect|save|forget");
}
#endif
