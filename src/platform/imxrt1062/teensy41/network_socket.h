#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SK_NET_CHUNK 512
// All payloads are copied. The Ethernet worker never retains interpreter memory.
enum sk_net_op { SK_NET_OPEN, SK_NET_CLOSE, SK_NET_CLOSE_ALL, SK_NET_CONNECT,
    SK_NET_CONNECTED, SK_NET_SEND, SK_NET_RECV, SK_NET_DNS_START, SK_NET_DNS_POLL };
typedef struct { uint32_t id; int op, handle, length; uint16_t port; uint8_t ip[4];
    char host[254]; uint8_t data[SK_NET_CHUNK]; } sk_net_request;
typedef struct { uint32_t id; int error, value; uint8_t ip[4], data[SK_NET_CHUNK]; } sk_net_reply;
int sk_net_call(sk_net_request *request, sk_net_reply *reply);
void sk_net_transport_begin(void);
void sk_net_transport_poll(int ready);
void sk_net_transport_reset(void);
void sk_python_network_init(void);
void sk_python_network_close_all(void);
#ifdef __cplusplus
}
#endif
