#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SOLAR_DNP3_RELAY_CAPACITY 4096
/* Nonblocking callbacks: byte count, 0 for EOF, or negative errno. Serial reads
 * return -EAGAIN when idle. Writes never accept more than the supplied length.
 */
typedef struct {
  void *user;
  int (*read)(void *, uint8_t *, size_t);
  int (*write)(void *, const uint8_t *, size_t);
  int (*half_close)(void *);
} solar_dnp3_endpoint_t;
typedef struct {
  uint8_t data[SOLAR_DNP3_RELAY_CAPACITY];
  size_t head, length;
  bool eof, output_closed;
  uint32_t received, forwarded, backpressure;
} solar_dnp3_pipe_t;
typedef struct {
  solar_dnp3_pipe_t pipe[2];
} solar_dnp3_bridge_t;
typedef void (*solar_dnp3_observe_t)(void *, unsigned, const uint8_t *, size_t);
/* 0 running, 1 both directions drained/closed, negative errno on failure. */
int solar_dnp3_bridge_step(solar_dnp3_bridge_t *,
                           const solar_dnp3_endpoint_t endpoints[2],
                           solar_dnp3_observe_t, void *);
#ifdef __cplusplus
}
#endif
