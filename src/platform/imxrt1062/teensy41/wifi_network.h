#pragma once
#include <stdint.h>
struct netif;
void sk_wifi_network_poll(bool lwip_ready);
struct netif *sk_wifi_netif();
bool sk_wifi_connecting();
uint32_t sk_wifi_dns();
// Snapshot of the normal network poll; never queries the bridge from the UI.
struct sk_wifi_status_t {
    bool available;
    uint8_t state;
    int8_t rssi;
    char ssid[33];
};
void sk_wifi_status(sk_wifi_status_t *out);
