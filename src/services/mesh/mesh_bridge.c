#include "mesh_bridge.h"
#include <stdio.h>
#include <string.h>
static uint32_t hash(const solar_mesh_event_t *e) {
  uint32_t h = 2166136261u;
  for (const char *p = e->text; *p; ++p)
    h = (h ^ (uint8_t)*p) * 16777619u;
  return h;
}
bool solar_mesh_bridge_prepare(solar_mesh_bridge_t *b,
                               const solar_mesh_event_t *e, const char *driver,
                               unsigned limit, uint32_t now, char *out,
                               size_t cap) {
  if (e->kind != SOLAR_MESH_MESSAGE || e->direct || e->channel != b->channel ||
      strstr(e->text, "[bridge "))
    return false;
  uint32_t h = hash(e);
  for (unsigned i = 0; i < 32; ++i)
    if (b->hashes[i] == h && (uint32_t)(now - b->times[i]) < 120000) {
      ++b->dropped;
      return false;
    }
  if (b->forwarded && (uint32_t)(now - b->last) < 10000) {
    ++b->dropped;
    return false;
  }
  int n = snprintf(out, cap, "[bridge %s] %s", driver, e->text);
  if (n < 0 || (size_t)n >= cap || (unsigned)n > limit) {
    ++b->dropped;
    return false;
  }
  return true;
}
void solar_mesh_bridge_complete(solar_mesh_bridge_t *b,
                                const solar_mesh_event_t *e, uint32_t now,
                                solar_mesh_result_t result) {
  if (result != SOLAR_MESH_OK) {
    ++b->dropped;
    return;
  }
  b->hashes[b->next] = hash(e);
  b->times[b->next] = now;
  b->next = (b->next + 1) % 32;
  b->last = now;
  ++b->forwarded;
}
