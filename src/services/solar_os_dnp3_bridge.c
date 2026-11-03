#include "solar_os_dnp3_bridge.h"
#include <errno.h>
#include <string.h>

static bool again(int n) {
  return n == -EAGAIN || n == -EWOULDBLOCK || n == -EINTR;
}
static int drain(solar_dnp3_pipe_t *p, const solar_dnp3_endpoint_t *out) {
  if (p->length) {
    size_t n = SOLAR_DNP3_RELAY_CAPACITY - p->head;
    if (n > p->length)
      n = p->length;
    if (n > 512)
      n = 512;
    int sent = out->write(out->user, p->data + p->head, n);
    if (sent < 0 && !again(sent))
      return sent;
    if (sent > 0) {
      if ((size_t)sent > n)
        return -EIO;
      p->head = (p->head + (size_t)sent) % SOLAR_DNP3_RELAY_CAPACITY;
      p->length -= sent;
      p->forwarded += sent;
    } else
      p->backpressure++;
  }
  if (p->eof && !p->length && !p->output_closed) {
    int result = out->half_close ? out->half_close(out->user) : 0;
    if (result < 0)
      return again(result) ? 0 : result;
    p->output_closed = true;
  }
  return 0;
}
int solar_dnp3_bridge_step(solar_dnp3_bridge_t *b,
                           const solar_dnp3_endpoint_t endpoints[2],
                           solar_dnp3_observe_t observe, void *user) {
  for (unsigned i = 0; i < 2; i++) {
    solar_dnp3_pipe_t *p = &b->pipe[i];
    int result = drain(p, &endpoints[1 - i]);
    if (result < 0)
      return result;
    if (!p->eof && p->length < SOLAR_DNP3_RELAY_CAPACITY) {
      uint8_t data[256];
      size_t space = SOLAR_DNP3_RELAY_CAPACITY - p->length;
      if (space > sizeof(data))
        space = sizeof(data);
      int n = endpoints[i].read(endpoints[i].user, data, space);
      if (n < 0 && !again(n))
        return n;
      if (n == 0)
        p->eof = true;
      if (n > 0) {
        if ((size_t)n > space)
          return -EIO;
        size_t tail = (p->head + p->length) % SOLAR_DNP3_RELAY_CAPACITY;
        size_t first = SOLAR_DNP3_RELAY_CAPACITY - tail;
        if (first > (size_t)n)
          first = n;
        memcpy(p->data + tail, data, first);
        memcpy(p->data, data + first, (size_t)n - first);
        p->length += n;
        p->received += n;
        /* Forward before decoding, and never filter/transform the stream. */
        result = drain(p, &endpoints[1 - i]);
        if (observe)
          observe(user, i, data, n);
        if (result < 0)
          return result;
      }
    }
  }
  return b->pipe[0].output_closed && b->pipe[1].output_closed ? 1 : 0;
}
