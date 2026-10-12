#pragma once
#include "solar_os_mesh_client.h"
#ifdef __cplusplus
extern "C" {
#endif
// Shared background service. Applications never own or read the physical UART.
bool solar_os_mesh_service_status(unsigned slot, solar_mesh_status_t *status);
bool solar_os_mesh_service_event(unsigned slot, uint64_t *cursor,
                                 solar_mesh_event_t *event, uint64_t *lost);
solar_mesh_result_t solar_os_mesh_service_send(unsigned slot, unsigned channel,
                                               const char *destination,
                                               const char *text);
// Coalesced notifications, one bit per slot (0..2). The event history is the
// queue; each consumer drains it using its own cursor. Notifications do not
// consume data. A subscription has one owner/waiter. Unsubscribe only after
// that waiter stops.
typedef struct solar_os_mesh_subscription solar_os_mesh_subscription_t;
solar_os_mesh_subscription_t *solar_os_mesh_subscribe(unsigned slot_mask);
unsigned solar_os_mesh_wait(solar_os_mesh_subscription_t *subscription,
                            uint32_t timeout_ms);
void solar_os_mesh_unsubscribe(solar_os_mesh_subscription_t *subscription);
#ifdef __cplusplus
}
#endif
