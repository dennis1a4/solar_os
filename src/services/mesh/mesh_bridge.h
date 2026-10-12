#pragma once
#include "solar_os_mesh_client.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  unsigned channel, next;
  uint32_t last, forwarded, dropped, hashes[32], times[32];
} solar_mesh_bridge_t;
// Returns false for excluded, duplicate, oversized or rate-limited events.
// Caller passes send()'s result to complete(); there is no automatic retry.
bool solar_mesh_bridge_prepare(solar_mesh_bridge_t *,
                               const solar_mesh_event_t *, const char *driver,
                               unsigned destination_limit, uint32_t now,
                               char *out, size_t cap);
void solar_mesh_bridge_complete(solar_mesh_bridge_t *,
                                const solar_mesh_event_t *, uint32_t now,
                                solar_mesh_result_t result);
#ifdef __cplusplus
}
#endif
