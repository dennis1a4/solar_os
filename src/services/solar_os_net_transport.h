#pragma once
/* Port boundary beneath the shared session service. All operations are
 * nonblocking with respect to the network except resolve() and wait(), which
 * honor their timeout/cancellation contracts. Errors use errno. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
int solar_os_net_transport_resolve(const char *host, char *ip, size_t size, uint32_t timeout_ms, bool (*cancel)(void *), void *user);
int solar_os_net_transport_open(bool udp, uint16_t local_port);
void solar_os_net_transport_close(int handle);
int solar_os_net_transport_connect(int handle, const char *ip, uint16_t port);
int solar_os_net_transport_wait(int handle, bool write, uint32_t timeout_ms);
ssize_t solar_os_net_transport_send(int handle, const void *data, size_t size);
ssize_t solar_os_net_transport_recv(int handle, void *data, size_t size);
ssize_t solar_os_net_transport_sendto(int handle, const char *ip, uint16_t port, const void *data, size_t size);
ssize_t solar_os_net_transport_recvfrom(int handle, void *data, size_t size, char *address, size_t address_size, uint16_t *port);

#ifdef __cplusplus
}
#endif
