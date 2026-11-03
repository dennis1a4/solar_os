#if SK_DNP3
#include "board.h"
#include "network_socket.h"
#include "platform.h"
#include <arduino_freertos.h>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <semphr.h>
extern "C" {
#include "solar_os_buses.h"
#include "solar_os_dnp3.h"
#include "solar_os_memory.h"
#include "solar_os_task.h"
}
struct DnpEndpoint {
  int handle, listener;
  bool uart, eof, output_closed;
};
struct solar_os_dnp3 {
  dnp_lab_t lab;
  solar_os_dnp3_status_t status;
  StaticSemaphore_t mutex_storage;
  SemaphoreHandle_t mutex;
  TaskHandle_t worker;
  volatile bool stop, done;
  DnpEndpoint endpoint[2];
  sk_net_request request;
  sk_net_reply reply;
  uint8_t scratch[512];
};
static constexpr const char *owner = "dnp3-lab";
static uint32_t now_ms() { return millis(); }
dnp_lab_t *solar_os_dnp3_lock(solar_os_dnp3_t *s,
                              solar_os_dnp3_status_t *status) {
  xSemaphoreTake(s->mutex, portMAX_DELAY);
  if (status)
    *status = s->status;
  return &s->lab;
}
void solar_os_dnp3_unlock(solar_os_dnp3_t *s) { xSemaphoreGive(s->mutex); }
static int rpc(solar_os_dnp3_t *s, int op, int handle = 0) {
  memset(&s->request, 0, sizeof(s->request));
  s->request.op = op;
  s->request.handle = handle;
  return sk_net_call(&s->request, &s->reply);
}
static const char *bus(unsigned slot) {
  return slot == 0 ? "uart7" : slot == 1 ? "uart8" : "uart3";
}
static void detail(solar_os_dnp3_t *s, const char *text) {
  solar_os_dnp3_lock(s, nullptr);
  snprintf(s->status.detail, sizeof(s->status.detail), "%s", text);
  dnp_lab_log(&s->lab, 0, "STATE", text, nullptr, 0);
  solar_os_dnp3_unlock(s);
}
static void close_endpoint(solar_os_dnp3_t *s, unsigned i) {
  auto &e = s->endpoint[i];
  if (e.uart)
    sk_uart_release(s->lab.config.endpoint[i].slot, owner);
  else {
    if (e.handle)
      rpc(s, SK_NET_CLOSE, e.handle);
    if (e.listener)
      rpc(s, SK_NET_FTP_END, e.listener);
  }
  e = DnpEndpoint{};
  solar_os_dnp3_lock(s, nullptr);
  s->status.connected[i] = false;
  solar_os_dnp3_unlock(s);
}
static bool open_endpoint(solar_os_dnp3_t *s, unsigned i) {
  const auto &c = s->lab.config.endpoint[i];
  auto &e = s->endpoint[i];
  if (c.kind == DNP_LAB_SERIAL) {
    esp_err_t error = sk_uart_claim_format(c.slot, owner, c.baud, c.format);
    if (error != ESP_OK) {
      detail(
          s,
          "UART unavailable/reserved; stop its bridge/mesh/logger owner first");
      return false;
    }
    e.uart = true;
    e.handle = 1;
  } else if (c.kind == DNP_LAB_LISTEN) {
    memset(&s->request, 0, sizeof(s->request));
    s->request.op = SK_NET_FTP_LISTEN;
    s->request.length = 1; // opt-in half-close for this listener
    s->request.port = c.port;
    if (sk_net_call(&s->request, &s->reply)) {
      detail(s, "TCP listen failed: port/listener slots unavailable");
      return false;
    }
    e.listener = s->reply.value;
    return true;
  } else {
    if (rpc(s, SK_NET_SERVICE_OPEN)) {
      detail(s, "No TCP channel available");
      return false;
    }
    e.handle = s->reply.value;
    if (rpc(s, SK_NET_HALF_CLOSE, e.handle)) {
      detail(s, "Cannot enable TCP half-close");
      return false;
    }
    int err = sk_net_resolve(
        c.host, s->scratch, 3000,
        [](void *p) { return static_cast<solar_os_dnp3_t *>(p)->stop; }, s);
    if (err) {
      detail(s, "Cannot resolve TCP peer");
      return false;
    }
    memset(&s->request, 0, sizeof(s->request));
    s->request.op = SK_NET_CONNECT;
    s->request.handle = e.handle;
    s->request.port = c.port;
    memcpy(s->request.ip, s->scratch, 4);
    err = sk_net_call(&s->request, &s->reply);
    if (err && err != EINPROGRESS && err != EAGAIN) {
      detail(s, "TCP connect failed");
      return false;
    }
    uint32_t began = now_ms();
    while (!s->stop && (err = rpc(s, SK_NET_CONNECTED, e.handle)) == EAGAIN &&
           now_ms() - began < 3000)
      vTaskDelay(1);
    if (err || s->stop) {
      detail(s, "TCP connect timeout/cancelled");
      return false;
    }
  }
  solar_os_dnp3_lock(s, nullptr);
  s->status.connected[i] = true;
  solar_os_dnp3_unlock(s);
  return true;
}
static int receive(solar_os_dnp3_t *s, unsigned i, uint8_t *p, size_t n) {
  auto &e = s->endpoint[i];
  if (e.eof)
    return -EAGAIN;
  if (!e.handle) {
    int err = rpc(s, SK_NET_FTP_ACCEPT, e.listener);
    if (err)
      return -err;
    e.handle = s->reply.value;
    solar_os_dnp3_lock(s, nullptr);
    s->status.connected[i] = true;
    solar_os_dnp3_unlock(s);
  }
  if (e.uart) {
    size_t got = 0;
    esp_err_t err = solar_os_bus_uart_read(bus(s->lab.config.endpoint[i].slot),
                                           p, n, 0, &got);
    return err == ESP_OK ? (got ? (int)got : -EAGAIN) : -EIO;
  }
  memset(&s->request, 0, sizeof(s->request));
  s->request.op = SK_NET_RECV;
  s->request.handle = e.handle;
  s->request.length = n;
  int err = sk_net_call(&s->request, &s->reply);
  if (err)
    return -err;
  memcpy(p, s->reply.data, s->reply.value);
  return s->reply.value;
}
static int transmit(solar_os_dnp3_t *s, unsigned i, const uint8_t *p,
                    size_t n) {
  auto &e = s->endpoint[i];
  if (!e.handle)
    return -EAGAIN;
  if (e.uart) {
    size_t sent = 0;
    esp_err_t err = solar_os_bus_uart_write(bus(s->lab.config.endpoint[i].slot),
                                            p, n, &sent);
    return (err == ESP_OK || err == ESP_ERR_TIMEOUT) ? (int)sent : -EIO;
  }
  if (n > SK_NET_CHUNK)
    n = SK_NET_CHUNK;
  memset(&s->request, 0, sizeof(s->request));
  s->request.op = SK_NET_SEND;
  s->request.handle = e.handle;
  s->request.length = n;
  memcpy(s->request.data, p, n);
  int err = sk_net_call(&s->request, &s->reply);
  return err ? -err : s->reply.value;
}
static bool would_block(int n) { return n == -EAGAIN || n == -EWOULDBLOCK; }
static void run(void *p) {
  auto *s = static_cast<solar_os_dnp3_t *>(p);
  auto mode = s->lab.config.mode;
  if (mode != DNP_LAB_DEMO) {
    if ((mode != DNP_LAB_MASTER && !open_endpoint(s, 0)) ||
        (mode != DNP_LAB_OUTSTATION && !open_endpoint(s, 1)))
      goto done;
  }
  detail(s, mode == DNP_LAB_DEMO
                ? "Demo loopback master/outstation; no external I/O"
                : "Running; transport independent of display");
  while (!s->stop) {
    for (unsigned dir = 0; dir < 2 && !s->stop; dir++) {
      if (mode == DNP_LAB_MASTER && dir == 0)
        continue;
      if (mode == DNP_LAB_OUTSTATION && dir == 1)
        continue;
      solar_os_dnp3_lock(s, nullptr);
      size_t room = dnp_lab_receive_space(&s->lab, dir);
      solar_os_dnp3_unlock(s);
      if (room > 128)
        room = 128;
      if (!room)
        continue;
      if (mode != DNP_LAB_DEMO) {
        int n = receive(s, dir, s->scratch, room);
        if (n > 0) {
          solar_os_dnp3_lock(s, nullptr);
          dnp_lab_feed(&s->lab, dir, s->scratch, n, now_ms());
          solar_os_dnp3_unlock(s);
        } else if (n == 0) {
          s->endpoint[dir].eof = true;
          detail(s, "Peer EOF; draining both directions");
        } else if (!would_block(n)) {
          detail(s, "Transport read error; stopped (restart explicitly)");
          s->stop = true;
        }
      }
    }
    solar_os_dnp3_lock(s, nullptr);
    dnp_lab_step(&s->lab, now_ms());
    solar_os_dnp3_unlock(s);
    for (unsigned dir = 0; dir < 2 && !s->stop; dir++)
      for (unsigned batch = 0; batch < 4; batch++) {
        solar_os_dnp3_lock(s, nullptr);
        const auto *q = dnp_lab_output(&s->lab, dir, now_ms());
        size_t n = q ? q->length - q->offset : 0;
        if (n)
          memcpy(s->scratch, q->bytes + q->offset, n);
        solar_os_dnp3_unlock(s);
        if (!n)
          break;
        int sent;
        if (mode == DNP_LAB_DEMO) {
          solar_os_dnp3_lock(s, nullptr);
          sent =
              dnp_lab_feed(&s->lab, dir, s->scratch, n, now_ms()) ? (int)n : 0;
          solar_os_dnp3_unlock(s);
        } else
          sent = transmit(s, 1 - dir, s->scratch, n);
        if (sent > 0) {
          solar_os_dnp3_lock(s, nullptr);
          dnp_lab_sent(&s->lab, dir, sent);
          solar_os_dnp3_unlock(s);
        } else if (sent < 0 && !would_block(sent)) {
          detail(s, "Transport write error; stopped (restart explicitly)");
          s->stop = true;
        } else {
          solar_os_dnp3_lock(s, nullptr);
          s->lab.stats[dir].backpressure++;
          solar_os_dnp3_unlock(s);
          break;
        }
      }
    if (mode == DNP_LAB_BRIDGE) {
      for (unsigned dir = 0; dir < 2; dir++) {
        if (s->endpoint[dir].eof && !s->endpoint[1 - dir].output_closed) {
          solar_os_dnp3_lock(s, nullptr);
          bool drained =
              !s->lab.direction[dir].used && !s->lab.direction[dir].count;
          solar_os_dnp3_unlock(s);
          if (drained) {
            auto &out = s->endpoint[1 - dir];
            if (!out.uart && out.handle)
              rpc(s, SK_NET_CLOSE_OUTPUT, out.handle);
            out.output_closed = true;
          }
        }
      }
      if (s->endpoint[0].eof && s->endpoint[1].eof &&
          s->endpoint[0].output_closed && s->endpoint[1].output_closed)
        break;
    } else if (mode != DNP_LAB_DEMO &&
               (s->endpoint[0].eof || s->endpoint[1].eof))
      break;
    vTaskDelay(1);
  }
done:
  close_endpoint(s, 0);
  close_endpoint(s, 1);
  solar_os_dnp3_lock(s, nullptr);
  s->status.running = false;
  solar_os_dnp3_unlock(s);
  s->done = true;
  for (;;)
    vTaskSuspend(nullptr);
}
esp_err_t solar_os_dnp3_create(solar_os_dnp3_t **out) {
  auto *s = static_cast<solar_os_dnp3_t *>(
      solar_os_memory_alloc(sizeof(solar_os_dnp3_t),
                            SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "dnp3.service"));
  if (!s)
    return ESP_ERR_NO_MEM;
  memset(s, 0, sizeof(*s));
  s->mutex = xSemaphoreCreateMutexStatic(&s->mutex_storage);
  if (!s->mutex) {
    solar_os_memory_free(s);
    return ESP_ERR_NO_MEM;
  }
  dnp_lab_config_t config;
  dnp_lab_defaults(&config);
  dnp_lab_init(&s->lab, &config);
  strcpy(s->status.detail, "Stopped; configure endpoints, then start");
  *out = s;
  return ESP_OK;
}
void solar_os_dnp3_halt(solar_os_dnp3_t *s) {
  if (!s || !s->worker)
    return;
  s->stop = true;
  while (!solar_os_task_wait_done(s->worker, &s->done, 1000))
    vTaskDelay(1);
  s->status.worker_stack_free =
      uxTaskGetStackHighWaterMark(s->worker) * sizeof(StackType_t);
  solar_os_task_delete_external(s->worker);
  s->worker = nullptr;
}
void solar_os_dnp3_destroy(solar_os_dnp3_t *s) {
  if (!s)
    return;
  solar_os_dnp3_halt(s);
  vSemaphoreDelete(s->mutex);
  solar_os_memory_free(s);
}
bool solar_os_dnp3_command(solar_os_dnp3_t *s, const char *line, char *out,
                           size_t n) {
  if (!strcmp(line, "stop")) {
    solar_os_dnp3_halt(s);
    snprintf(out, n, "Stopped and leases released");
    return true;
  }
  if (!strcmp(line, "start")) {
    solar_os_dnp3_lock(s, nullptr);
    bool running = s->status.running;
    solar_os_dnp3_unlock(s);
    if (running) {
      snprintf(out, n, "Already running");
      return false;
    }
    solar_os_dnp3_halt(s);
    auto &c = s->lab.config;
    if (c.mode == DNP_LAB_BRIDGE && c.endpoint[0].kind == DNP_LAB_SERIAL &&
        c.endpoint[1].kind == DNP_LAB_SERIAL &&
        c.endpoint[0].slot == c.endpoint[1].slot) {
      snprintf(out, n, "Bridge endpoints must use distinct serial ports");
      return false;
    }
    solar_os_dnp3_lock(s, nullptr);
    for (unsigned i = 0; i < 2; i++) {
      s->lab.direction[i].used = 0;
      s->lab.direction[i].head = s->lab.direction[i].count = 0;
      s->lab.assembly[i].active = false;
      dnp3_init(&s->lab.direction[i].decode, s->lab.direction[i].decode.event,
                s->lab.direction[i].decode.user);
    }
    s->lab.waiting = s->lab.pending_operate = s->lab.selected =
        s->lab.have_rx_fcb = false;
    s->lab.remote_request_length = s->lab.remote_reply_length = 0;
    s->lab.last_poll = now_ms();
    s->lab.now = now_ms();
    s->stop = s->done = false;
    s->status.running = true;
    solar_os_dnp3_unlock(s);
    if (solar_os_task_create_pinned_external(
            run, "dnp3", 12288, s, 2, &s->worker, 0,
            SOLAR_OS_TASK_ROLE_SYSTEM) != pdPASS) {
      s->status.running = false;
      snprintf(out, n, "Cannot allocate DNP3 worker");
      return false;
    }
    snprintf(out, n, "Starting; inspect connection status");
    return true;
  }
  solar_os_dnp3_lock(s, nullptr);
  s->lab.now = now_ms();
  bool ok = dnp_lab_command(&s->lab, line, s->status.running, out, n);
  if (ok)
    dnp_lab_log(&s->lab, 0, "CONFIG", line, nullptr, 0);
  solar_os_dnp3_unlock(s);
  return ok;
}
#endif
