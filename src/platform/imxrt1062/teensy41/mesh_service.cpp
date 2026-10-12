#if SK_MESH_CLIENT
#include "mesh_service.h"
#include "mesh_bridge.h"
#include "platform.h"
#include <arduino_freertos.h>
#include <event_groups.h>
#include <semphr.h>
extern "C" {
#include "solar_os.h"
#include "solar_os_buses.h"
#include "solar_os_memory.h"
#include "solar_os_shell_io.h"
}
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *const uart_names[] = {"uart7", "uart8", "uart3"};
static const char *const owners[] = {"mesh:slot0", "mesh:slot1", "mesh:slot2"};
static solar_mesh_client_t *radios[3];
static StaticSemaphore_t state_storage, life_storage;
static SemaphoreHandle_t state_lock, life_lock;
static StaticTask_t radio_tcb, log_tcb;
static TaskHandle_t radio_task, log_task;
struct Guard {
  SemaphoreHandle_t mutex;
  explicit Guard(SemaphoreHandle_t m) : mutex(m) {
    xSemaphoreTake(m, portMAX_DELAY);
  }
  ~Guard() { xSemaphoreGive(mutex); }
};
struct solar_os_mesh_subscription {
  StaticEventGroup_t storage;
  EventGroupHandle_t group;
  unsigned mask;
};
static solar_os_mesh_subscription_t *subscriptions[8];
// Invoked under state_lock, after the new event is visible in the history ring.
static void notify_slot(void *user) {
  unsigned bit = 1U << (uintptr_t)user;
  for (auto *s : subscriptions)
    if (s && (s->mask & bit))
      xEventGroupSetBits(s->group, bit);
}
extern "C" solar_os_mesh_subscription_t *
solar_os_mesh_subscribe(unsigned mask) {
  if (!mask || (mask & ~7U))
    return nullptr;
  auto *s = (solar_os_mesh_subscription_t *)solar_os_memory_alloc(
      sizeof(solar_os_mesh_subscription_t), SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,
      "mesh-subscription");
  if (!s)
    return nullptr;
  s->mask = mask;
  s->group = xEventGroupCreateStatic(&s->storage);
  Guard state(state_lock);
  for (auto &entry : subscriptions)
    if (!entry) {
      entry = s;
      xEventGroupSetBits(s->group, mask);
      return s;
    }
  vEventGroupDelete(s->group);
  solar_os_memory_free(s);
  return nullptr;
}
extern "C" unsigned solar_os_mesh_wait(solar_os_mesh_subscription_t *s,
                                       uint32_t timeout) {
  if (!s)
    return 0;
  return xEventGroupWaitBits(s->group, s->mask, pdTRUE, pdFALSE,
                             pdMS_TO_TICKS(timeout)) &
         s->mask;
}
extern "C" void solar_os_mesh_unsubscribe(solar_os_mesh_subscription_t *s) {
  if (!s)
    return;
  Guard state(state_lock);
  for (auto &entry : subscriptions)
    if (entry == s) {
      entry = nullptr;
      vEventGroupDelete(s->group);
      solar_os_memory_free(s);
      return;
    }
}
static solar_os_mesh_subscription_t *log_subscription;
struct Log {
  FILE *file = nullptr;
  uint64_t cursor = 0, lost = 0, stored = 0;
  bool failed = false;
  char path[128]{};
};
static Log logs[3]; // protected by life_lock; writer takes state_lock only to
                    // copy events
struct Bridge {
  bool active = false;
  unsigned source = 0, destination = 0, from_channel = 0, to_channel = 0;
  uint64_t cursor = 0, lost = 0;
  solar_mesh_bridge_t policy{};
};
static Bridge bridge;
static int write_radio(void *arg, const uint8_t *bytes, size_t count) {
  unsigned slot = (uintptr_t)arg;
  size_t written = 0;
  if (count > 128)
    count = 128;
  esp_err_t err =
      solar_os_bus_uart_write(uart_names[slot], bytes, count, &written);
  return err == ESP_OK || err == ESP_ERR_TIMEOUT ? (int)written : -1;
}
static void bridge_step() {
  if (!bridge.active)
    return;
  auto *source = radios[bridge.source],
       *destination = radios[bridge.destination];
  solar_mesh_status_t s{}, d{};
  if (source)
    solar_mesh_status(source, &s);
  if (destination)
    solar_mesh_status(destination, &d);
  if (!s.ready || !d.ready) {
    bridge.active = false;
    return;
  }
  solar_mesh_event_t e{};
  for (unsigned i = 0;
       i < 8 && solar_mesh_event(source, &bridge.cursor, &e, &bridge.lost);
       ++i) {
    char text[SOLAR_MESH_TEXT + 1];
    uint32_t now = millis();
    if (!solar_mesh_bridge_prepare(&bridge.policy, &e, s.driver, d.text_limit,
                                   now, text, sizeof(text)))
      continue;
    auto result = solar_mesh_send(destination, bridge.to_channel, nullptr, text,
                                  (uint32_t)time(nullptr));
    solar_mesh_bridge_complete(&bridge.policy, &e, now, result);
  }
}
static void radio_run(void *) {
  for (;;) {
    bool active = false;
    {
      Guard lock(state_lock);
      uint8_t bytes[128];
      for (unsigned slot = 0; slot < 3; ++slot) {
        auto *c = radios[slot];
        if (!c)
          continue;
        active = true;
        for (unsigned batch = 0; batch < 8; ++batch) {
          size_t n = 0;
          if (solar_os_bus_uart_read(uart_names[slot], bytes, sizeof(bytes), 0,
                                     &n) != ESP_OK ||
              !n)
            break;
          solar_mesh_feed(c, bytes, n, millis());
        }
        solar_mesh_tick(c, millis());
      }
      bridge_step();
    }
    if (active)
      vTaskDelay(pdMS_TO_TICKS(10));
    else
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
}
static bool write_event(Log &log, const solar_mesh_event_t &e) {
  // Text is sanitized by the portable core: no control characters/TSV row
  // injection.
  int n =
      fprintf(log.file, "%lu\t%lu\t%s\t%u\t%s\t%s\t", (unsigned long)e.time_ms,
              (unsigned long)e.timestamp, solar_mesh_event_name(e.kind),
              e.channel, e.direct ? "direct" : "channel", e.sender);
  if (e.has_rssi)
    n |= fprintf(log.file, "%d", e.rssi);
  n |= fprintf(log.file, "\t");
  if (e.has_snr)
    n |= fprintf(log.file, "%.2f", e.snr_q4 / 4.0);
  n |= fprintf(log.file, "\t%08lx\t%s\n", (unsigned long)e.packet_id, e.text);
  return n >= 0;
}
static void log_run(void *) {
  uint32_t flushed = millis();
  for (;;) {
    bool active = false;
    {
      Guard life(life_lock);
      for (unsigned slot = 0; slot < 3; ++slot) {
        auto &l = logs[slot];
        if (!l.file || l.failed)
          continue;
        active = true;
        for (unsigned batch = 0; batch < 32; ++batch) {
          solar_mesh_event_t e{};
          bool got;
          {
            Guard state(state_lock);
            got = radios[slot] &&
                  solar_mesh_event(radios[slot], &l.cursor, &e, &l.lost);
          }
          if (!got)
            break;
          if (!write_event(l, e)) {
            l.failed = true;
            break;
          }
          ++l.stored;
        }
        if (uint32_t(millis() - flushed) >= 250 && fflush(l.file) != 0)
          l.failed = true;
      }
    }
    if (uint32_t(millis() - flushed) >= 250)
      flushed = millis();
    if (active)
      solar_os_mesh_wait(log_subscription, 250);
    else
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
}
static bool start_task(TaskHandle_t &handle, StaticTask_t &tcb,
                       TaskFunction_t fn, const char *name, unsigned priority) {
  if (handle)
    return true;
  const unsigned words = 2048;
  auto *stack = (StackType_t *)solar_os_memory_alloc(
      words * sizeof(StackType_t), SOLAR_OS_MEMORY_INTERNAL_PREFERRED, name);
  if (!stack)
    return false;
  handle = xTaskCreateStatic(fn, name, words, nullptr, priority, stack, &tcb);
  if (!handle)
    solar_os_memory_free(stack);
  return handle != nullptr;
}
extern "C" void sk_mesh_init() {
  state_lock = xSemaphoreCreateMutexStatic(&state_storage);
  life_lock = xSemaphoreCreateMutexStatic(&life_storage);
  configASSERT(state_lock && life_lock);
}
static esp_err_t connect_radio(unsigned slot, const char *driver) {
  Guard life(life_lock);
  Guard state(state_lock);
  if (radios[slot])
    return ESP_ERR_INVALID_STATE;
  bool known = false;
  for (unsigned i = 0; solar_mesh_driver_name(i); ++i)
    if (!strcmp(driver, solar_mesh_driver_name(i)))
      known = true;
  if (!known)
    return ESP_ERR_INVALID_ARG;
  auto *c = (solar_mesh_client_t *)solar_os_memory_alloc(
      solar_mesh_client_size(), SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,
      "mesh-radio");
  if (!c)
    return ESP_ERR_NO_MEM;
  esp_err_t err = sk_uart_claim(slot, owners[slot], 115200);
  if (err != ESP_OK) {
    solar_os_memory_free(c);
    return err;
  }
  if (!start_task(radio_task, radio_tcb, radio_run, "mesh-radio", 2)) {
    sk_uart_release(slot, owners[slot]);
    solar_os_memory_free(c);
    return ESP_ERR_NO_MEM;
  }
  solar_mesh_init(c, driver, write_radio, (void *)(uintptr_t)slot, millis(),
                  micros() ^ (uint32_t)time(nullptr));
  radios[slot] = c;
  solar_mesh_set_notify(c, notify_slot, (void *)(uintptr_t)slot);
  notify_slot((void *)(uintptr_t)slot);
  xTaskNotifyGive(radio_task);
  return ESP_OK;
}
static bool close_log(unsigned slot) {
  auto &l = logs[slot];
  bool ok = !l.failed;
  if (l.file) {
    for (unsigned i = 0; i < SOLAR_MESH_EVENTS && !l.failed; ++i) {
      solar_mesh_event_t e{};
      bool got;
      {
        Guard state(state_lock);
        got = radios[slot] &&
              solar_mesh_event(radios[slot], &l.cursor, &e, &l.lost);
      }
      if (!got)
        break;
      if (!write_event(l, e)) {
        l.failed = true;
        ok = false;
        break;
      }
      ++l.stored;
    }
    if (fflush(l.file) != 0)
      ok = false;
    if (fclose(l.file) != 0)
      ok = false;
    l.file = nullptr;
  }
  l.failed = !ok;
  return ok;
}
static bool disconnect_radio(unsigned slot) {
  Guard life(life_lock);
  bool ok = close_log(slot);
  Guard state(state_lock);
  if (bridge.active && (bridge.source == slot || bridge.destination == slot))
    bridge.active = false;
  if (radios[slot]) {
    sk_uart_release(slot, owners[slot]);
    solar_os_memory_free(radios[slot]);
    radios[slot] = nullptr;
    notify_slot((void *)(uintptr_t)slot);
  }
  return ok;
}
extern "C" bool sk_mesh_shutdown() {
  bool ok = true;
  for (unsigned i = 0; i < 3; ++i)
    ok = disconnect_radio(i) && ok;
  return ok;
}
extern "C" bool solar_os_mesh_service_status(unsigned slot,
                                             solar_mesh_status_t *s) {
  if (slot >= 3 || !s)
    return false;
  Guard state(state_lock);
  if (!radios[slot])
    return false;
  solar_mesh_status(radios[slot], s);
  return true;
}
extern "C" bool solar_os_mesh_service_event(unsigned slot, uint64_t *cursor,
                                            solar_mesh_event_t *e,
                                            uint64_t *lost) {
  if (slot >= 3)
    return false;
  Guard state(state_lock);
  return radios[slot] && solar_mesh_event(radios[slot], cursor, e, lost);
}
extern "C" solar_mesh_result_t solar_os_mesh_service_send(unsigned slot,
                                                          unsigned channel,
                                                          const char *dest,
                                                          const char *text) {
  if (slot >= 3)
    return SOLAR_MESH_ARGUMENT;
  Guard state(state_lock);
  return radios[slot] ? solar_mesh_send(radios[slot], channel, dest, text,
                                        (uint32_t)time(nullptr))
                      : SOLAR_MESH_OFFLINE;
}
static int slot_number(const char *s) {
  return s && strlen(s) == 5 && !strncmp(s, "slot", 4) && s[4] >= '0' &&
                 s[4] <= '2'
             ? s[4] - '0'
             : -1;
}
static int channel_number(const char *s) {
  return s && strlen(s) == 1 && *s >= '0' && *s < '0' + SOLAR_MESH_CHANNELS
             ? *s - '0'
             : -1;
}
static void status(solar_os_shell_io_t *io, int slot) {
  solar_mesh_status_t s{};
  if (!solar_os_mesh_service_status(slot, &s)) {
    solar_os_shell_io_printf(io, "slot%d: disconnected (%s, 115200 baud)\n",
                             slot, uart_names[slot]);
    return;
  }
  solar_os_shell_io_printf(
      io,
      "slot%d: %s %s, %s at 115200\nname: %s  id: %s\nfirmware: %s  %s\nframes "
      "rx/tx: %lu/%lu  messages rx/tx: %lu/%lu\nnodes: %u (omitted %lu)  "
      "channels: %u  text limit: %u bytes\nmalformed: %lu  timeouts: %lu  "
      "PSRAM: %u bytes\n",
      slot, s.driver,
      s.ready      ? "ready"
      : s.error[0] ? "failed"
                   : "connecting",
      uart_names[slot], s.name, s.node_id, s.firmware, s.model,
      (unsigned long)s.frames_rx, (unsigned long)s.frames_tx,
      (unsigned long)s.messages_rx, (unsigned long)s.messages_tx, s.node_count,
      (unsigned long)s.node_overflow, s.channel_count, s.text_limit,
      (unsigned long)s.malformed, (unsigned long)s.timeouts,
      (unsigned)solar_mesh_client_size());
  solar_os_shell_io_printf(io, "last RSSI: ");
  if (s.has_rssi)
    solar_os_shell_io_printf(io, "%d dBm", s.rssi);
  else
    solar_os_shell_io_printf(io, "unavailable");
  solar_os_shell_io_printf(io, "  SNR: ");
  if (s.has_snr)
    solar_os_shell_io_printf(io, "%.2f dB", s.snr_q4 / 4.0);
  else
    solar_os_shell_io_printf(io, "unavailable");
  solar_os_shell_io_printf(io, "\n%s\n", s.error);
}
extern "C" void sk_shell_cmd_mesh(solar_os_context_t *ctx, int argc,
                                  char **argv) {
  auto *io = solar_os_context_shell_io(ctx);
  io->command_status = 1;
  const char *op = argc > 1 ? argv[1] : "status";
  int offset = argc > 1 ? 2 : 1;
  if (!strcmp(argv[0], "meshstatus")) {
    op = "status";
    offset = 1;
  }
  if (!strcmp(argv[0], "meshlog")) {
    op = "log";
    offset = 1;
  }
  if (!strcmp(argv[0], "meshbridge")) {
    op = "bridge";
    offset = 1;
  }
  int n = argc - offset;
  char **a = argv + offset;
  if (!strcmp(op, "drivers") && n == 0) {
    solar_os_shell_io_writeln(
        io, "meshcore, meshtastic (runtime selection; 115200 8N1)");
    io->command_status = 0;
    return;
  }
  if (!strcmp(op, "status") && n <= 1) {
    int slot = n == 1 ? slot_number(a[0]) : -1;
    if (n == 1 && slot < 0)
      goto usage;
    for (int i = 0; i < 3; ++i)
      if (slot < 0 || slot == i)
        status(io, i);
    io->command_status = 0;
    return;
  }
  if (!strcmp(op, "bridge")) {
    Guard state(state_lock);
    if (n == 1 && !strcmp(a[0], "off")) {
      bridge.active = false;
      io->command_status = 0;
      return;
    }
    if (n == 0 || (n == 1 && !strcmp(a[0], "status"))) {
      solar_os_shell_io_printf(
          io,
          "bridge %s: slot%u channel %u -> slot%u channel %u; forwarded %lu, "
          "dropped %lu, history lost %llu\n",
          bridge.active ? "ON" : "off", bridge.source, bridge.from_channel,
          bridge.destination, bridge.to_channel,
          (unsigned long)bridge.policy.forwarded,
          (unsigned long)bridge.policy.dropped,
          (unsigned long long)bridge.lost);
      io->command_status = 0;
      return;
    }
    if (n != 5 || strcmp(a[0], "on"))
      goto usage;
    int source = slot_number(a[1]), from = channel_number(a[2]),
        dest = slot_number(a[3]), to = channel_number(a[4]);
    if (source < 0 || dest < 0 || from < 0 || to < 0 || source == dest)
      goto usage;
    solar_mesh_status_t s{}, d{};
    if (radios[source])
      solar_mesh_status(radios[source], &s);
    if (radios[dest])
      solar_mesh_status(radios[dest], &d);
    if (!s.ready || !d.ready || !strcmp(s.driver, d.driver)) {
      solar_os_shell_io_writeln(
          io, "Bridge needs two ready radios using different drivers.");
      return;
    }
    bridge = Bridge{};
    bridge.source = source;
    bridge.destination = dest;
    bridge.from_channel = from;
    bridge.policy.channel = from;
    bridge.to_channel = to;
    solar_mesh_event_t e{};
    while (solar_mesh_event(radios[source], &bridge.cursor, &e, nullptr)) {
    }
    bridge.active = true;
    solar_os_shell_io_writeln(
        io, "One-way bridge enabled for NEW channel messages; limit one per 10 "
            "s. Direct messages are excluded.");
    io->command_status = 0;
    return;
  }
  if (!strcmp(op, "log")) {
    if (n < 2)
      goto usage;
    int slot = slot_number(a[1]);
    if (slot < 0)
      goto usage;
    Guard life(life_lock);
    auto &l = logs[slot];
    if (!strcmp(a[0], "stop") && n == 2) {
      io->command_status = close_log(slot) ? 0 : 1;
      return;
    }
    if (!strcmp(a[0], "status") && n == 2) {
      solar_os_shell_io_printf(
          io, "slot%d log %s %s: stored %llu, lost %llu%s\n", slot,
          l.file ? "ON" : "off", l.path, (unsigned long long)l.stored,
          (unsigned long long)l.lost, l.failed ? " WRITE FAILED" : "");
      io->command_status = l.failed ? 1 : 0;
      return;
    }
    if (strcmp(a[0], "start") || n != 3 || strncmp(a[2], "/sd/", 4) ||
        strlen(a[2]) >= sizeof(l.path) || strstr(a[2], "/../") ||
        strlen(a[2]) < 5)
      goto usage;
    if (l.file) {
      solar_os_shell_io_writeln(io, "Log already active; stop it first.");
      return;
    }
    {
      Guard state(state_lock);
      if (!radios[slot]) {
        solar_os_shell_io_writeln(io, "Connect the slot first.");
        return;
      }
    }
    if (!log_subscription)
      log_subscription = solar_os_mesh_subscribe(7);
    if (!log_subscription) {
      solar_os_shell_io_writeln(
          io, "Cannot allocate log notification subscription.");
      return;
    }
    if (!start_task(log_task, log_tcb, log_run, "mesh-log", 1)) {
      solar_os_shell_io_writeln(io, "Cannot allocate log task.");
      return;
    }
    FILE *file = fopen(a[2], "wbx");
    if (!file) {
      solar_os_shell_io_writeln(
          io, "Cannot create log (use a new file on mounted SD).");
      return;
    }
    l = Log{};
    l.file = file;
    snprintf(l.path, sizeof(l.path), "%s", a[2]);
    {
      Guard state(state_lock);
      solar_mesh_event_t e{};
      while (solar_mesh_event(radios[slot], &l.cursor, &e, nullptr)) {
      }
    }
    if (fputs("uptime_ms\ttimestamp\tkind\tchannel\tscope\tsender\trssi_"
              "dbm\tsnr_db\tpacket_id\ttext\n",
              file) < 0) {
      l.failed = true;
      close_log(slot);
      return;
    }
    xTaskNotifyGive(log_task);
    io->command_status = 0;
    return;
  }
  if (n < 1)
    goto usage;
  {
    int slot = slot_number(a[0]);
    if (slot < 0)
      goto usage;
    if (!strcmp(op, "connect") && n == 2) {
      esp_err_t e = connect_radio(slot, a[1]);
      solar_os_shell_io_printf(
          io, "mesh: %s%s\n", esp_err_to_name(e),
          e == ESP_OK ? "; handshake started (meshstatus shows progress)"
                      : "; check slot claims, driver and available memory");
      io->command_status = e == ESP_OK ? 0 : 1;
      return;
    }
    if (!strcmp(op, "disconnect") && n == 1) {
      io->command_status = disconnect_radio(slot) ? 0 : 1;
      return;
    }
    if (!strcmp(op, "refresh") && n == 1) {
      Guard state(state_lock);
      solar_mesh_result_t r =
          radios[slot] ? solar_mesh_refresh(radios[slot]) : SOLAR_MESH_OFFLINE;
      solar_os_shell_io_writeln(io, solar_mesh_result_name(r));
      io->command_status = r == SOLAR_MESH_OK ? 0 : 1;
      return;
    }
    if ((!strcmp(op, "nodes") || !strcmp(op, "channels")) && n == 1) {
      Guard state(state_lock);
      if (!radios[slot]) {
        solar_os_shell_io_writeln(io, "Slot disconnected.");
        return;
      }
      if (!strcmp(op, "nodes")) {
        solar_mesh_node_t node;
        for (unsigned i = 0; solar_mesh_node(radios[slot], i, &node); ++i)
          solar_os_shell_io_printf(io, "%s  %s\n", node.id, node.name);
      } else {
        solar_mesh_channel_t ch;
        for (unsigned i = 0; i < SOLAR_MESH_CHANNELS; ++i)
          if (solar_mesh_channel(radios[slot], i, &ch))
            solar_os_shell_io_printf(io, "%u %s %s\n", i,
                                     ch.enabled ? "enabled" : "disabled",
                                     ch.name);
      }
      io->command_status = 0;
      return;
    }
    bool direct = !strcmp(op, "dm");
    if ((direct || !strcmp(op, "send")) && n >= (direct ? 4 : 3)) {
      int channel = channel_number(a[1]);
      if (channel < 0)
        goto usage;
      char text[SOLAR_MESH_TEXT + 1]{};
      size_t used = 0;
      for (int i = direct ? 3 : 2; i < n; ++i) {
        size_t len = strlen(a[i]);
        if (used + len + (used ? 1 : 0) >= sizeof(text)) {
          solar_os_shell_io_writeln(io, "Message too long.");
          return;
        }
        if (used)
          text[used++] = ' ';
        memcpy(text + used, a[i], len + 1);
        used += len;
      }
      auto r = solar_os_mesh_service_send(slot, channel,
                                          direct ? a[2] : nullptr, text);
      solar_os_shell_io_writeln(io, solar_mesh_result_name(r));
      io->command_status = r == SOLAR_MESH_OK ? 0 : 1;
      return;
    }
  }
usage:
  solar_os_shell_io_writeln(
      io,
      "mesh drivers | status [slotN]\nmesh connect slotN "
      "meshcore|meshtastic\nmesh disconnect|refresh|nodes|channels slotN\nmesh "
      "send slotN CHANNEL TEXT...\nmesh dm slotN CHANNEL NODE_ID TEXT...\nchat "
      "slotN [CHANNEL] | meshmon slotN\nmeshlog start slotN /sd/NEWFILE.tsv | "
      "stop|status slotN\nmeshbridge on SOURCE_SLOT SOURCE_CHANNEL DEST_SLOT "
      "DEST_CHANNEL\nmeshbridge off|status\nSlots: slot0 UART7 RX28/TX29; "
      "slot1 UART8 RX34/TX35; slot2 UART3 RX15/TX14 (display conflict).");
}
#endif
