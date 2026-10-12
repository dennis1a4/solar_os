#if SK_WIRELESS_BRIDGE
#include "wireless_bridge.h"
#include "audio_output.h"
#include "keyboard_input.h"
#include "platform.h"
#include "board.h"
#include <arduino_freertos.h>
#include <semphr.h>
extern "C" {
#include "solar_os.h"
#include "solar_os_resources.h"
#include "solar_os_shell_io.h"
#include "wireless/bridge_wire.h"
}
#include <atomic>
#include <stdlib.h>
static const char *owner = "wireless:slot0";
// Only the pending-data hint reads the slot without holding mutex.
static std::atomic<unsigned> selected_slot{0};
static const superkeyboard::SlotPins &slot_pins() {
  return superkeyboard::slots[selected_slot.load()];
}
static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t mutex;
static StaticTask_t worker_tcb;
static TaskHandle_t worker_handle;
DMAMEM static StackType_t worker_stack[1024];
static std::atomic<bool> enabled{false}, healthy{false}, bt_route{false},
    playing{false}, audio_ready{false};
static bool claimed[2];
static uint8_t volume = 20, status_bytes[30];
static uint16_t sequence;
static uint8_t tx[WB_SIZE], rx[WB_SIZE], request[WB_SIZE];
static TeensyKeyboardInput keyboard;
static uint32_t key_generation;
static unsigned failures;
static volatile bool transfer_completed;
static void handshake_fell() { transfer_completed = true; }
static uint8_t audio_pending[WB_PAYLOAD];
static size_t audio_pending_frames;
extern "C" bool sk_console_audio_busy();
struct Guard {
  Guard() { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(mutex); }
};
static bool transfer() {
  uint32_t began = millis();
  while (!digitalRead(slot_pins().tx)) {
    if (millis() - began >= 250)
      return false;
    vTaskDelay(1);
  }
  transfer_completed = false;
  if (sk_slot_spi(selected_slot.load(), owner, WB_SPI_HZ, WB_SPI_MODE, tx, rx, WB_SIZE) != ESP_OK)
    return false;
  // Capture the falling edge: ESP flash writes may delay completion and a
  // fast low/high readiness pulse must not be missed between transactions.
  began = millis();
  while (!transfer_completed) {
    if (millis() - began >= 250)
      return false;
    vTaskDelay(1);
  }
  return true;
}
/* All callers hold mutex. Bounded stop-and-wait; exact duplicate retry is
 * idempotent on ESP (especially PCM). Unknown outcomes fail the stream. */
static unsigned rpc(unsigned op, const void *data, size_t size, uint8_t *out,
                    size_t capacity, size_t *length = nullptr) {
  if (!enabled)
    return WB_NOT_READY;
  if (size > WB_PAYLOAD)
    return WB_INVALID;
  uint16_t seq = ++sequence;
  if (!seq)
    seq = ++sequence;
  wb_pack(request, 0, seq, op, 0, data, size);
  for (unsigned retry = 0; retry < 2; retry++) {
    memcpy(tx, request, WB_SIZE);
    if (!transfer())
      continue;
    for (unsigned poll = 0; poll < 3; poll++) {
      wb_pack(tx, 0, 0, WB_NOP, 0, nullptr, 0);
      if (!transfer())
        break;
      if (!wb_valid(rx, WB_SIZE) || rx[3] != 1 || wb_u16(rx + 4) != seq ||
          wb_u16(rx + 6) != op)
        continue;
      size_t n = wb_u16(rx + 8);
      if (n > capacity)
        return WB_INVALID;
      if (n && out)
        memcpy(out, rx + WB_HEADER, n);
      if (length)
        *length = n;
      healthy = true;
      return wb_u16(rx + 10);
    }
  }
  healthy = false;
  audio_ready = false;
  ++failures;
  taskENTER_CRITICAL();
  keyboard.disconnect();
  taskEXIT_CRITICAL();
  return WB_FAILED;
}
extern "C" bool sk_wireless_wifi_pending() { return enabled && healthy && digitalRead(slot_pins().rx); }
extern "C" bool sk_wireless_wifi_available() { return enabled && healthy; }
extern "C" unsigned sk_wireless_wifi_command(unsigned op, const void *in, size_t n,
    uint8_t *out, size_t capacity, size_t *len) {
  if (!mutex) return WB_NOT_READY;
  Guard g;
  unsigned r = rpc(op, in, n, out, capacity, len);
  // Credentials must not remain in the transport's reusable buffers.
  if (op == WB_WIFI_JOIN) {
    volatile uint8_t *p = request;
    for(size_t i=0;i<sizeof(request);++i) p[i]=0;
    p=tx; for(size_t i=0;i<sizeof(tx);++i) p[i]=0;
  }
  return r;
}
extern "C" unsigned sk_wireless_wifi_tx(const uint8_t *frame, size_t length) {
  if (!frame || length<14 || length>1514) return WB_INVALID;
  if (!mutex) return WB_NOT_READY;
  Guard g;
  static uint8_t chunk[WB_PAYLOAD];
  for(size_t offset=0;offset<length;) {
    size_t bytes=length-offset;
    if(bytes>WB_PAYLOAD-4) bytes=WB_PAYLOAD-4;
    wb_p16(chunk,length); wb_p16(chunk+2,offset);
    memcpy(chunk+4,frame+offset,bytes);
    unsigned r=rpc(WB_WIFI_TX,chunk,bytes+4,nullptr,0);
    if(r) return r;
    offset+=bytes;
  }
  return WB_OK;
}
extern "C" unsigned sk_wireless_wifi_rx(uint8_t *frame,size_t capacity,size_t *length) {
  if (!frame || !length || !mutex) return WB_INVALID;
  *length=0;
  Guard g;
  static uint8_t chunk[WB_PAYLOAD];
  size_t total=0, offset=0;
  do {
    uint8_t q[2]; wb_p16(q,offset); size_t n=0;
    unsigned r=rpc(WB_WIFI_RX,q,2,chunk,sizeof(chunk),&n);
    if(r) return r;
    if(!n && !offset) return WB_OK;
    if(n<5 || wb_u16(chunk)<14 || wb_u16(chunk)>1514 ||
       wb_u16(chunk)>capacity || wb_u16(chunk+2)!=offset) return WB_FAILED;
    if(!offset) total=wb_u16(chunk);
    if(wb_u16(chunk)!=total || n-4>total-offset) return WB_FAILED;
    memcpy(frame+offset,chunk+4,n-4); offset+=n-4;
  } while(offset<total);
  *length=total; return WB_OK;
}
static bool get_status() {
  size_t n = 0;
  unsigned e =
      rpc(WB_STATUS, nullptr, 0, status_bytes, sizeof(status_bytes), &n);
  if (e != WB_OK || n != 30)
    return false;
  audio_ready = status_bytes[0] & 8;
  return true;
}
static void stop_link() {
  enabled = healthy = audio_ready = false;
  playing = false;
  bt_route = false;
  taskENTER_CRITICAL();
  keyboard.disconnect();
  taskEXIT_CRITICAL();
  for (unsigned i = 0; i < 2; i++)
    if (claimed[i]) {
      int pin = i ? slot_pins().tx : slot_pins().rx;
      if (i == 1)
        detachInterrupt(pin);
      pinMode(pin, INPUT);
      solar_os_resource_release(SOLAR_OS_RESOURCE_GPIO_PIN, pin, -1, owner);
      claimed[i] = false;
    }
  sk_slot_release(selected_slot.load(), owner);
}
static bool start_link(unsigned slot = 0) {
  if (slot > 1)
    return false;
  if (enabled)
    return selected_slot.load() == slot && get_status();
  selected_slot = slot;
  owner = slot ? "wireless:slot1" : "wireless:slot0";
  if (sk_slot_claim(slot, owner) != ESP_OK)
    return false;
  for (unsigned i = 0; i < 2; i++) {
    if (solar_os_resource_claim(SOLAR_OS_RESOURCE_GPIO_PIN, i ? slot_pins().tx : slot_pins().rx, -1, owner,
                                "bridge handshake") != ESP_OK) {
      stop_link();
      return false;
    }
    claimed[i] = true;
  }
  pinMode(slot_pins().rx, INPUT_PULLDOWN);
  pinMode(slot_pins().tx, INPUT_PULLDOWN);
  attachInterrupt(slot_pins().tx, handshake_fell, FALLING);
  enabled = true;
  if (!get_status()) {
    stop_link();
    return false;
  }
  xTaskNotifyGive(worker_handle);
  return true;
}
static void poll(void *) {
  uint32_t heartbeat = 0;
  for (;;) {
    if (xSemaphoreTake(mutex, 0) == pdTRUE) {
      if (enabled && millis() - heartbeat >= (healthy ? 1000u : 5000u)) {
        get_status();
        heartbeat = millis();
      }
      if (enabled && healthy && digitalRead(slot_pins().rx)) {
        uint8_t report[13];
        size_t n = 0;
        if (rpc(WB_KEYS, nullptr, 0, report, sizeof(report), &n) == WB_OK &&
            n == 13) {
          taskENTER_CRITICAL();
          if (!report[0] || key_generation != wb_u32(report + 1)) {
            keyboard.disconnect();
            key_generation = wb_u32(report + 1);
          }
          if (report[0]) {
            const uint8_t usages[] = {83, 57, 71};
            for (unsigned i = 0; i < 3; i++) {
              bool pressed = false;
              for (unsigned j = 7; j < 13; j++)
                pressed |= report[j] == usages[i];
              if (pressed && !keyboard.is_down(usages[i]))
                keyboard.leds ^= uint8_t(1u << i);
            }
            keyboard.boot_report(report + 5, millis());
          }
          taskEXIT_CRITICAL();
        } else {
          taskENTER_CRITICAL();
          keyboard.disconnect();
          taskEXIT_CRITICAL();
        }
      }
      xSemaphoreGive(mutex);
    }
    ulTaskNotifyTake(pdTRUE, enabled ? pdMS_TO_TICKS(10) : portMAX_DELAY);
  }
}
extern "C" void sk_wireless_init() {
  mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
  configASSERT(mutex);
  worker_handle = xTaskCreateStatic(poll, "wireless", 1024, nullptr, 2,
                                    worker_stack, &worker_tcb);
  configASSERT(worker_handle);
}
extern "C" bool sk_wireless_shutdown() {
  if (!mutex)
    return true;
  Guard g;
  if (enabled)
    stop_link();
  return true;
}
extern "C" int sk_wireless_key_read() {
  taskENTER_CRITICAL();
  int k = keyboard.read(millis());
  taskEXIT_CRITICAL();
  return k;
}
extern "C" void sk_wireless_key_boundary() {
  taskENTER_CRITICAL();
  keyboard.boundary();
  taskEXIT_CRITICAL();
}
extern "C" bool sk_wireless_audio_selected() { return bt_route; }
extern "C" bool sk_wireless_audio_ready() {
  return bt_route && enabled && healthy && audio_ready;
}
extern "C" esp_err_t sk_wireless_audio_start(uint8_t v) {
  Guard g;
  if (!get_status() || !audio_ready)
    return ESP_ERR_NOT_FOUND;
  volume = v == 255 ? 20 : v;
  if (volume > 100)
    return ESP_ERR_INVALID_ARG;
  if (playing)
    return ESP_ERR_INVALID_STATE;
  audio_pending_frames = 0;
  playing = rpc(WB_AUDIO_START, nullptr, 0, nullptr, 0) == WB_OK;
  return playing ? ESP_OK : ESP_FAIL;
}
extern "C" esp_err_t sk_wireless_audio_volume(uint8_t v) {
  if (v > 100)
    return ESP_ERR_INVALID_ARG;
  Guard g;
  volume = v;
  return ESP_OK;
}
static bool cancelled(const volatile bool *stop) {
  return stop ? *stop : sk_audio_cancelled();
}
static esp_err_t flush_audio(const volatile bool *stop) {
  uint32_t began = millis();
  for (;;) {
    unsigned r;
    {
      Guard g;
      if (!playing || !healthy)
        return ESP_ERR_INVALID_STATE;
      if (!audio_pending_frames)
        return ESP_OK;
      r = rpc(WB_AUDIO_PCM, audio_pending, audio_pending_frames * 4, nullptr,
              0);
      if (r == WB_OK) {
        audio_pending_frames = 0;
        return ESP_OK;
      }
    }
    if (r != WB_BUSY)
      return ESP_FAIL;
    if (millis() - began > 1500 || cancelled(stop))
      return ESP_ERR_TIMEOUT;
    vTaskDelay(1);
  }
}
static esp_err_t write_audio(const int16_t *data, size_t frames,
                             const volatile bool *stop) {
  if (!data && frames)
    return ESP_ERR_INVALID_ARG;
  while (frames) {
    if (cancelled(stop))
      return ESP_ERR_TIMEOUT;
    size_t count;
    bool full;
    {
      Guard g;
      if (!playing || !healthy)
        return ESP_ERR_INVALID_STATE;
      count = WB_PAYLOAD / 4 - audio_pending_frames;
      if (count > frames)
        count = frames;
      for (size_t i = 0; i < count * 2; i++)
        wb_p16(audio_pending + audio_pending_frames * 4 + i * 2,
               (uint16_t)((int32_t)data[i] * volume / 100));
      audio_pending_frames += count;
      full = audio_pending_frames == WB_PAYLOAD / 4;
    }
    data += count * 2;
    frames -= count;
    if (full) {
      esp_err_t e = flush_audio(stop);
      if (e != ESP_OK)
        return e;
    }
  }
  return ESP_OK;
}
extern "C" esp_err_t sk_wireless_audio_write(const int16_t *data,
                                             size_t frames) {
  return write_audio(data, frames, nullptr);
}
extern "C" esp_err_t sk_wireless_audio_synth_write(const int16_t *data,
                                                   size_t frames,
                                                   const volatile bool *stop) {
  return write_audio(data, frames, stop);
}
extern "C" esp_err_t sk_wireless_audio_finish(bool drain) {
  esp_err_t flushed = ESP_OK;
  if (drain && playing)
    flushed = flush_audio(nullptr);
  if (flushed != ESP_OK)
    drain = false;
  {
    Guard g;
    audio_pending_frames = 0;
  }
  uint32_t began = millis();
  for (;;) {
    unsigned r;
    {
      Guard g;
      if (!playing)
        return ESP_OK;
      uint8_t d = drain;
      r = rpc(WB_AUDIO_STOP, &d, 1, nullptr, 0);
      if (r != WB_BUSY)
        playing = false;
    }
    if (r == WB_OK)
      return flushed;
    if (r != WB_BUSY)
      return ESP_FAIL;
    if (millis() - began > 2000 || sk_audio_cancelled()) {
      Guard g;
      uint8_t d = 0;
      rpc(WB_AUDIO_STOP, &d, 1, nullptr, 0);
      playing = false;
      return ESP_ERR_TIMEOUT;
    }
    vTaskDelay(1);
  }
}
static void print_result(solar_os_shell_io_t *io, unsigned r) {
  static const char *names[] = {"ok",        "invalid request",
                                "busy",      "link/device error",
                                "not ready", "unsupported"};
  solar_os_shell_io_printf(io, "bridge: %s\n",
                           r < 6 ? names[r] : "protocol error");
  io->command_status = r ? 1 : 0;
}
static bool number(const char *s, unsigned max, unsigned &v) {
  if (!s || !*s)
    return false;
  v = 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9' || v > (max - unsigned(*s - '0')) / 10 ||
        unsigned(*s - '0') > max)
      return false;
    v = v * 10 + unsigned(*s - '0');
  }
  return v <= max;
}
static const char *wifi_security(unsigned mode) {
  static const char *names[] = {
      "open", "WEP",       "WPA",      "WPA2",       "WPA/WPA2",    "EAP",
      "WPA3", "WPA2/WPA3", "WAPI",     "OWE",        "WPA3-EAP192", "WPA3",
      "WPA3", "DPP",       "WPA3-EAP", "WPA2/3-EAP", "WPA-EAP"};
  return mode < sizeof(names) / sizeof(*names) ? names[mode] : "unknown";
}
static void printable(char *dst, const uint8_t *src, size_t n) {
  for (size_t i = 0; i < n; i++)
    dst[i] = src[i] >= 32 && src[i] < 127 ? src[i] : '.';
  dst[n] = 0;
}
static void status(solar_os_shell_io_t *io) {
  if (!enabled) {
    solar_os_shell_io_writeln(io, "bridge off; run wbridge on after wiring");
    return;
  }
  if (!get_status()) {
    print_result(io, WB_FAILED);
    return;
  }
  solar_os_shell_io_printf(io,
                           "SPI%u slot%u 6MHz mode3; keyboard=%s audio=%s "
                           "route=%s underruns=%lu errors=%u\n",
                           unsigned(slot_pins().spi), selected_slot.load(),
                           status_bytes[0] & 4 ? "connected" : "absent",
                           status_bytes[0] & 8 ? "connected" : "absent",
                           bt_route ? "bluetooth" : "local",
                           (unsigned long)wb_u32(status_bytes + 12), failures);
  if (status_bytes[3])
    solar_os_shell_io_printf(
        io,
        "Pair request type %u, value %06lu; bt confirm yes|no, bt passkey "
        "NNNNNN, or bt pin DIGITS (30s timeout)\n",
        status_bytes[3], (unsigned long)wb_u32(status_bytes + 4));
  if (wb_u32(status_bytes + 26))
    solar_os_shell_io_writeln(io,
                              "Keyboard sent unsupported non-boot reports; "
                              "boot-compatible 8-byte reports are required.");
}
extern "C" void sk_shell_cmd_wbridge(solar_os_context_t *ctx, int argc,
                                     char **argv) {
  auto *io = solar_os_context_shell_io(ctx);
  Guard g;
  io->command_status = 0;
  if ((argc == 2 || argc == 3) && !strcmp(argv[1], "on")) {
    unsigned slot = 0;
    if (argc == 3) {
      if (!strcmp(argv[2], "slot1")) slot = 1;
      else if (strcmp(argv[2], "slot0")) {
        io->command_status = 1;
        solar_os_shell_io_writeln(io, "Choose slot0 or slot1");
        return;
      }
    }
    if (enabled && selected_slot.load() != slot) {
      io->command_status = 1;
      solar_os_shell_io_writeln(io, "Bridge active on another slot; stop playback and run wbridge off first");
      return;
    }
    print_result(io, start_link(slot) ? WB_OK : WB_FAILED);
    return;
  }
  if (argc == 2 && !strcmp(argv[1], "off")) {
    if (playing) {
      print_result(io, WB_BUSY);
      return;
    }
    if (enabled)
      stop_link();
    return;
  }
  if (argc == 1 || (argc == 2 && !strcmp(argv[1], "status"))) {
    status(io);
    return;
  }
  io->command_status = 1;
  solar_os_shell_io_writeln(io, "usage: wbridge on [slot0|slot1] | off | status");
}
extern "C" void sk_shell_cmd_bridge_wifi(solar_os_context_t *ctx, int argc,
                                         char **argv) {
  auto *io = solar_os_context_shell_io(ctx);
  io->command_status = 1;
  if (argc != 2 || strcmp(argv[1], "scan")) {
    extern void sk_wifi_shell(solar_os_context_t *, int, char **);
    sk_wifi_shell(ctx, argc, argv);
    return;
  }
  {
    Guard g;
    unsigned r = rpc(WB_WIFI_SCAN, nullptr, 0, nullptr, 0);
    if (r) {
      print_result(io, r);
      return;
    }
  }
  uint32_t began = millis();
  for (;;) {
    bool scanning;
    {
      Guard g;
      if (!get_status()) {
        print_result(io, WB_FAILED);
        return;
      }
      scanning = status_bytes[0] & 1;
    }
    if (!scanning)
      break;
    if (millis() - began > 15000) {
      solar_os_shell_io_writeln(io, "Wi-Fi scan timed out");
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  Guard g;
  solar_os_shell_io_writeln(
      io, "SSID                              dBm security channel");
  for (uint8_t i = 0; i < status_bytes[1]; i++) {
    uint8_t ap[42];
    size_t n = 0;
    unsigned r = rpc(WB_WIFI_RESULT, &i, 1, ap, sizeof(ap), &n);
    if (r || n != 42) {
      print_result(io, r ? r : unsigned(WB_INVALID));
      return;
    }
    char name[33];
    size_t l = 0;
    while (l < 32 && ap[l])
      l++;
    printable(name, ap, l);
    solar_os_shell_io_printf(io, "%-32s %4d %-12s %u\n", l ? name : "<hidden>",
                             (int8_t)ap[33], wifi_security(ap[34]), ap[35]);
  }
  io->command_status = 0;
}
extern "C" void sk_shell_cmd_bridge_bt(solar_os_context_t *ctx, int argc,
                                       char **argv) {
  auto *io = solar_os_context_shell_io(ctx);
  io->command_status = 1;
  if (argc == 1 || (argc == 2 && !strcmp(argv[1], "status"))) {
    Guard g;
    io->command_status = 0;
    status(io);
    return;
  }
  if ((argc == 2 || argc == 3) && !strcmp(argv[1], "scan")) {
    uint8_t ble = argc == 3 && !strcmp(argv[2], "ble");
    if (argc == 3 && !ble)
      goto usage;
    {
      Guard g;
      unsigned r = rpc(WB_BT_SCAN, &ble, 1, nullptr, 0);
      if (r) {
        print_result(io, r);
        return;
      }
    }
    uint32_t began = millis();
    for (;;) {
      bool scanning;
      {
        Guard g;
        if (!get_status()) {
          print_result(io, WB_FAILED);
          return;
        }
        scanning = status_bytes[0] & 2;
      }
      if (!scanning)
        break;
      if (millis() - began > 12000) {
        solar_os_shell_io_writeln(io, "Bluetooth scan timed out");
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    Guard g;
    for (uint8_t i = 0; i < status_bytes[2]; i++) {
      uint8_t d[76];
      size_t n = 0;
      unsigned r = rpc(WB_BT_RESULT, &i, 1, d, sizeof(d), &n);
      if (r || n != 76) {
        print_result(io, r ? r : unsigned(WB_INVALID));
        return;
      }
      char name[65];
      size_t l = 0;
      while (l < 64 && d[9 + l])
        l++;
      printable(name, d + 9, l);
      solar_os_shell_io_printf(
          io, "%u %s %02x:%02x:%02x:%02x:%02x:%02x %d dBm %s\n", i,
          d[6] ? "BLE" : "BT", d[0], d[1], d[2], d[3], d[4], d[5], (int8_t)d[8],
          name);
    }
    io->command_status = 0;
    return;
  }
  if (argc == 3 &&
      (!strcmp(argv[1], "keyboard") || !strcmp(argv[1], "audio"))) {
    unsigned i;
    unsigned op =
        !strcmp(argv[1], "keyboard") ? WB_HID_CONNECT : WB_AUDIO_CONNECT;
    Guard g;
    if (!strcmp(argv[2], "disconnect")) {
      print_result(io, rpc(op == WB_HID_CONNECT ? WB_HID_CLOSE : WB_AUDIO_CLOSE,
                           nullptr, 0, nullptr, 0));
      return;
    }
    if (number(argv[2], 31, i)) {
      uint8_t v = i;
      print_result(io, rpc(op, &v, 1, nullptr, 0));
      return;
    }
    goto usage;
  }
  if (argc == 3 && !strcmp(argv[1], "output")) {
    Guard g;
    if (playing || sk_console_audio_busy()) {
      print_result(io, WB_BUSY);
      return;
    }
    if (!strcmp(argv[2], "local")) {
      bt_route = false;
      io->command_status = 0;
      return;
    }
    if (!strcmp(argv[2], "bluetooth")) {
      if (get_status() && audio_ready) {
        bt_route = true;
        io->command_status = 0;
      } else
        print_result(io, WB_NOT_READY);
      return;
    }
    goto usage;
  }
  if (argc == 3 && !strcmp(argv[1], "confirm")) {
    uint8_t v;
    if (!strcmp(argv[2], "yes"))
      v = 1;
    else if (!strcmp(argv[2], "no"))
      v = 0;
    else
      goto usage;
    Guard g;
    print_result(io, rpc(WB_PAIR, &v, 1, nullptr, 0));
    return;
  }
  if (argc == 3 && !strcmp(argv[1], "passkey")) {
    unsigned v;
    if (!number(argv[2], 999999, v))
      goto usage;
    uint8_t b[5] = {1};
    wb_p32(b + 1, v);
    Guard g;
    print_result(io, rpc(WB_PAIR, b, 5, nullptr, 0));
    return;
  }
  if (argc == 3 && !strcmp(argv[1], "pin")) {
    size_t n = strlen(argv[2]);
    if (!n || n > 16)
      goto usage;
    for (size_t i = 0; i < n; i++)
      if (argv[2][i] < '0' || argv[2][i] > '9')
        goto usage;
    uint8_t b[17] = {1};
    memcpy(b + 1, argv[2], n);
    Guard g;
    print_result(io, rpc(WB_PAIR, b, n + 1, nullptr, 0));
    return;
  }
usage:
  solar_os_shell_io_writeln(
      io, "bt scan [ble] | status | keyboard INDEX|disconnect | audio "
          "INDEX|disconnect\nbt confirm yes|no | passkey NNNNNN | pin DIGITS | "
          "output local|bluetooth");
}
#endif
