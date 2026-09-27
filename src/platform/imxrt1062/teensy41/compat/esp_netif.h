#pragma once
/* Narrow source-compatible netif view for the shared SolarOS registry.
 * This is cached metadata, not an ESP-IDF network stack. */
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sk_netif esp_netif_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { union { esp_ip4_addr_t ip4; } u_addr; uint8_t type; } esp_ip_addr_t;
typedef struct { esp_ip_addr_t ip; } esp_netif_dns_info_t;
typedef struct { esp_ip4_addr_t ip, netmask, gw; } esp_netif_ip_info_t;
#define ESP_NETIF_DNS_MAIN 0
char *esp_ip4addr_ntoa(const esp_ip4_addr_t *ip, char *buffer, int size);
esp_netif_t *esp_netif_get_default_netif(void);
void *esp_netif_get_netif_impl(esp_netif_t *netif);
int esp_netif_set_route_prio(esp_netif_t *netif, int priority);
esp_err_t esp_netif_get_dns_info(esp_netif_t *netif, int which, esp_netif_dns_info_t *dns);
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *ip);
#ifdef __cplusplus
}
#endif
