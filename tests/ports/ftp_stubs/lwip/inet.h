#pragma once
#include <stdint.h>
struct in_addr {uint32_t s_addr;};
typedef struct {uint32_t addr;} ip4_addr_t;
#define htons(v) __builtin_bswap16(v)
#define ntohs(v) __builtin_bswap16(v)
#define htonl(v) __builtin_bswap32(v)
#define ntohl(v) __builtin_bswap32(v)
#ifdef __cplusplus
extern "C" {
#endif
int ip4addr_aton(const char *,ip4_addr_t *);
#ifdef __cplusplus
}
#endif
