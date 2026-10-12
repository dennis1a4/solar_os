#pragma once
#include <stdint.h>
struct netif;
void sk_wifi_network_poll(bool lwip_ready);
struct netif *sk_wifi_netif();
bool sk_wifi_connecting();
uint32_t sk_wifi_dns();
