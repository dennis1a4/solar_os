// SolarOS Wi-Fi multi-interface configuration
#if SK_WIRELESS_BRIDGE
#undef LWIP_SINGLE_NETIF
#define LWIP_SINGLE_NETIF 0
#undef CHECKSUM_GEN_IP
#undef CHECKSUM_GEN_UDP
#undef CHECKSUM_GEN_TCP
#undef CHECKSUM_GEN_ICMP
#undef CHECKSUM_CHECK_IP
#undef CHECKSUM_CHECK_UDP
#undef CHECKSUM_CHECK_TCP
#undef CHECKSUM_CHECK_ICMP
#define CHECKSUM_GEN_IP 1
#define CHECKSUM_GEN_UDP 1
#define CHECKSUM_GEN_TCP 1
#define CHECKSUM_GEN_ICMP 1
#define CHECKSUM_CHECK_IP 1
#define CHECKSUM_CHECK_UDP 1
#define CHECKSUM_CHECK_TCP 1
#define CHECKSUM_CHECK_ICMP 1
#ifdef __cplusplus
extern "C" {
#endif
struct netif;
struct ip4_addr;
struct netif *sk_network_route(const struct ip4_addr *, const struct ip4_addr *);
#ifdef __cplusplus
}
#endif
#define LWIP_HOOK_IP4_ROUTE_SRC sk_network_route
#endif
