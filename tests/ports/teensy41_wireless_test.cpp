#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#define SK_WIRELESS_BRIDGE 1
#include "../../src/platform/imxrt1062/teensy41/wireless_bridge.cpp"
void sk_wifi_shell(solar_os_context_t *, int, char **) {}
uint32_t test_clock;
static bool ready_pin = true, slot_claimed = false, pin_claim[42], foreign29,
            corrupt_once, drop_all;
static unsigned pcm_commands, pcm_bytes;
static std::vector<uint8_t> received_pcm, wifi_sent, wifi_in;
static bool bad_wifi_offset;
static bool pcm_busy;
static uint8_t pending[WB_SIZE], last_req[WB_SIZE], last_resp[WB_SIZE];
static bool last_valid;
static std::string printed;
int digitalRead(int) { return ready_pin; }
void pinMode(int pin, int) { assert(!(pin == 29 && foreign29)); }
esp_err_t sk_slot_claim(unsigned, const char *) {
  slot_claimed = true;
  return ESP_OK;
}
esp_err_t sk_slot_release(unsigned, const char *) {
  slot_claimed = false;
  return ESP_OK;
}
extern "C" esp_err_t solar_os_resource_claim(solar_os_resource_kind_t, int pin,
                                             int, const char *, const char *) {
  if (pin == 29 && foreign29)
    return ESP_FAIL;
  pin_claim[pin] = true;
  return ESP_OK;
}
extern "C" esp_err_t solar_os_resource_release(solar_os_resource_kind_t,
                                               int pin, int, const char *) {
  bool had = pin_claim[pin];
  pin_claim[pin] = false;
  return had ? ESP_OK : ESP_FAIL;
}
extern "C" bool sk_console_audio_busy() { return false; }
extern "C" bool sk_audio_cancelled() { return false; }
extern "C" void solar_os_shell_io_writeln(solar_os_shell_io_t *,
                                          const char *s) {
  printed += s;
  printed += '\n';
}
extern "C" void solar_os_shell_io_printf(solar_os_shell_io_t *, const char *f,
                                         ...) {
  char b[512];
  va_list a;
  va_start(a, f);
  vsnprintf(b, sizeof(b), f, a);
  va_end(a);
  printed += b;
}
esp_err_t sk_slot_spi(unsigned, const char *, uint32_t hz, uint8_t mode,
                      const uint8_t *in, uint8_t *out, size_t n) {
  assert(slot_claimed && n == WB_SIZE);
  assert(hz == WB_SPI_HZ && mode == WB_SPI_MODE);
  handshake_fell();
  memcpy(out, pending, n);
  if (drop_all)
    memset(out, 0, n);
  if (corrupt_once && out[3]) {
    out[16] ^= 1;
    corrupt_once = false;
  }
  assert(wb_valid(in, n));
  unsigned op = wb_u16(in + 6);
  if (op == WB_NOP) {
    wb_pack(pending, 0, 0, WB_NOP, 0, nullptr, 0);
    return ESP_OK;
  }
  if (last_valid && !memcmp(last_req, in, n)) {
    memcpy(pending, last_resp, n);
    return ESP_OK;
  }
  uint8_t data[WB_PAYLOAD] = {};
  size_t len = 0;
  unsigned result = WB_OK;
  if (op == WB_STATUS) {
    data[0] = 8;
    data[1] = 1;
    data[2] = 1;
    wb_p32(data + 8, 32768);
    len = 30;
  } else if (op == WB_WIFI_RESULT) {
    memcpy(data, "test-network", 12);
    data[33] = (uint8_t)-55;
    data[34] = 3;
    data[35] = 6;
    len = 42;
  } else if (op == WB_BT_RESULT) {
    memcpy(data + 9, "speaker", 7);
    len = 76;
  } else if (op == WB_WIFI_TX) {
    unsigned offset=wb_u16(in+WB_HEADER+2);
    if(!offset) wifi_sent.clear();
    assert(offset==wifi_sent.size());
    wifi_sent.insert(wifi_sent.end(),in+WB_HEADER+4,in+WB_HEADER+wb_u16(in+8));
  } else if (op == WB_WIFI_RX && !wifi_in.empty()) {
    size_t offset=wb_u16(in+WB_HEADER);
    size_t count=wifi_in.size()-offset;
    if(count>WB_PAYLOAD-4) count=WB_PAYLOAD-4;
    wb_p16(data,wifi_in.size()); wb_p16(data+2,bad_wifi_offset?offset+1:offset);
    memcpy(data+4,wifi_in.data()+offset,count); len=count+4;
  } else if (op == WB_AUDIO_PCM) {
    if (pcm_busy)
      result = WB_BUSY;
    else {
      pcm_commands++;
      pcm_bytes += wb_u16(in + 8);
      received_pcm.insert(received_pcm.end(), in + WB_HEADER,
                          in + WB_HEADER + wb_u16(in + 8));
    }
  }
  wb_pack(pending, 1, wb_u16(in + 4), op, result, data, len);
  memcpy(last_req, in, n);
  memcpy(last_resp, pending, n);
  last_valid = true;
  return ESP_OK;
}
static void command(void (*fn)(solar_os_context_t *, int, char **),
                    std::initializer_list<const char *> words) {
  solar_os_shell_io_t io{};
  solar_os_context_t ctx{&io};
  std::vector<char *> argv;
  for (auto *w : words)
    argv.push_back(const_cast<char *>(w));
  printed.clear();
  fn(&ctx, argv.size(), argv.data());
  assert(io.command_status == 0);
}
int main() {
  sk_wireless_init();
  foreign29 = true;
  {
    Guard g;
    assert(!start_link());
  }
  assert(!pin_claim[28] && !slot_claimed);
  foreign29 = false;
  ready_pin = false;
  unsigned before = test_clock;
  {
    Guard g;
    assert(!start_link());
  }
  assert(test_clock - before <= 1000);
  assert(!pin_claim[28] && !pin_claim[29] && !slot_claimed);
  ready_pin = true;
  command(sk_shell_cmd_wbridge, {"wbridge", "on"});
  assert(enabled && healthy);
  command(sk_shell_cmd_bridge_wifi, {"wifi", "scan"});
  assert(printed.find("test-network") != std::string::npos);
  command(sk_shell_cmd_bridge_bt, {"bt", "scan"});
  assert(printed.find("speaker") != std::string::npos);
  command(sk_shell_cmd_bridge_bt, {"bt", "output", "bluetooth"});
  assert(sk_wireless_audio_ready());
  assert(sk_wireless_audio_start(100) == ESP_OK);
  int16_t samples[504] = {};
  corrupt_once = true;
  assert(sk_wireless_audio_write(samples, 252) == ESP_OK);
  assert(pcm_commands == 1 && pcm_bytes == 1008);
  assert(sk_wireless_audio_finish(true) == ESP_OK);
  assert(sk_wireless_audio_start(50) == ESP_OK);
  assert(sk_wireless_audio_write(samples, 8) == ESP_OK);
  assert(pcm_commands ==
         1); // small writes are batched, but finish must flush them
  assert(sk_wireless_audio_finish(true) == ESP_OK);
  assert(pcm_commands == 2 && pcm_bytes == 1040);
  assert(sk_wireless_audio_start(100) == ESP_OK);
  pcm_busy = true;
  unsigned busy_start = test_clock;
  assert(sk_wireless_audio_write(samples, 252) == ESP_ERR_TIMEOUT);
  assert(test_clock - busy_start <= 1600);
  pcm_busy = false;
  assert(sk_wireless_audio_finish(false) == ESP_OK);
  assert(pcm_commands ==
         2); // aborted partial/backpressured audio was discarded

  // Distinct left/right samples must survive scaling, packet boundaries,
  // short writes and a retried response without channel swaps or duplication.
  received_pcm.clear();
  std::vector<int16_t> stereo(1200);
  for (size_t f = 0; f < 600; ++f) {
    stereo[f * 2] = int16_t(1000 + f * 3);
    stereo[f * 2 + 1] = int16_t(-2000 - f * 5);
  }
  assert(sk_wireless_audio_start(25) == ESP_OK);
  corrupt_once = true;
  assert(sk_wireless_audio_write(stereo.data(), 17) == ESP_OK);
  assert(sk_wireless_audio_write(stereo.data() + 34, 583) == ESP_OK);
  assert(sk_wireless_audio_finish(true) == ESP_OK);
  assert(received_pcm.size() == stereo.size() * 2);
  for (size_t i = 0; i < stereo.size(); ++i)
    assert(wb_u16(received_pcm.data() + i * 2) ==
           uint16_t(int32_t(stereo[i]) * 25 / 100));

  // Maximum Ethernet frames cross SPI packets without truncation/duplication.
  std::vector<uint8_t> packet(1514), incoming(1514);
  for(size_t i=0;i<packet.size();i++) packet[i]=uint8_t(i*37);
  corrupt_once=true;
  assert(sk_wireless_wifi_tx(packet.data(),packet.size())==WB_OK);
  assert(wifi_sent==packet);
  assert(sk_wireless_wifi_tx(packet.data(),1515)==WB_INVALID);
  wifi_in=packet; size_t received=0;
  corrupt_once=true;
  assert(sk_wireless_wifi_rx(incoming.data(),incoming.size(),&received)==WB_OK);
  assert(received==packet.size() && incoming==packet);
  bad_wifi_offset=true;
  assert(sk_wireless_wifi_rx(incoming.data(),incoming.size(),&received)==WB_FAILED);
  assert(received==0);
  bad_wifi_offset=false; wifi_in.clear();
  assert(sk_wireless_wifi_rx(incoming.data(),incoming.size(),&received)==WB_OK && !received);
  uint8_t credentials[98]={};
  memset(credentials,42,sizeof(credentials));
  assert(sk_wireless_wifi_command(WB_WIFI_JOIN,credentials,sizeof(credentials),nullptr,0,nullptr)==WB_OK);
  for(auto b:request) assert(!b);
  for(auto b:tx) assert(!b);

  keyboard.press(4, test_clock);
  assert(sk_wireless_key_read() == 'a');
  keyboard.press(5, test_clock);
  drop_all = true;
  {
    Guard g;
    assert(!get_status());
  }
  assert(!healthy);
  assert(sk_wireless_key_read() == -1);
  drop_all = false;
  assert(sk_wireless_shutdown());
  assert(!enabled && !slot_claimed && !pin_claim[28] && !pin_claim[29]);
  puts("Teensy wireless commands, timeout, pin rollback, duplicate audio and "
       "disconnect tests passed");
}
