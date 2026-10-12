#include "solar_os_keys.h"
#include "solar_os_mesh_client.h"
#include "solar_os_mesh_service.h"
#include "solar_os_mesh_view.h"
#include "solar_os_tui_widgets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char screen[30][161];
static unsigned rows = 30, cols = 80;
static char sent[241];
static unsigned sent_slot, sent_channel;
size_t solar_os_tui_rows(const solar_os_tui_t *t) {
  (void)t;
  return rows;
}
size_t solar_os_tui_cols(const solar_os_tui_t *t) {
  (void)t;
  return cols;
}
esp_err_t solar_os_tui_screen_begin(solar_os_tui_t *t,
                                    solar_os_context_t *ctx) {
  (void)ctx;
  t->screen_active = true;
  return ESP_OK;
}
void solar_os_tui_end(solar_os_tui_t *t) { t->screen_active = false; }
void solar_os_tui_clear(solar_os_tui_t *t) {
  (void)t;
  memset(screen, 0, sizeof(screen));
}
void solar_os_tui_refresh(solar_os_tui_t *t) { (void)t; }
esp_err_t solar_os_tui_write_cell(solar_os_tui_t *t, size_t row, size_t col,
                                  size_t width, const char *text,
                                  uint8_t attr) {
  (void)t;
  (void)attr;
  assert(row < rows && col == 0 && width == cols);
  snprintf(screen[row], width + 1, "%s", text);
  return ESP_OK;
}
void solar_os_context_finish(solar_os_context_t *ctx, int code,
                             const char *message) {
  (void)message;
  ctx->exit_requested = true;
  ctx->exit_code = code;
}
bool solar_os_mesh_service_status(unsigned slot, solar_mesh_status_t *s) {
  memset(s, 0, sizeof(*s));
  s->ready = true;
  strcpy(s->driver, slot ? "meshtastic" : "meshcore");
  return true;
}
bool solar_os_mesh_service_event(unsigned slot, uint64_t *cursor,
                                 solar_mesh_event_t *e, uint64_t *lost) {
  (void)slot;
  (void)lost;
  if (*cursor)
    return false;
  ++*cursor;
  memset(e, 0, sizeof(*e));
  e->kind = SOLAR_MESH_MESSAGE;
  e->has_rssi = true;
  e->rssi = -99;
  memset(e->text, 'a', 160);
  strcpy(e->text + 160, " END_OF_LONG_MESSAGE");
  return true;
}
solar_mesh_result_t solar_os_mesh_service_send(unsigned slot, unsigned channel,
                                               const char *dest,
                                               const char *text) {
  assert(!dest);
  sent_slot = slot;
  sent_channel = channel;
  strcpy(sent, text);
  return SOLAR_MESH_OK;
}
const char *solar_mesh_result_name(solar_mesh_result_t result) {
  (void)result;
  return "OK";
}
const char *solar_mesh_event_name(solar_mesh_event_kind_t kind) {
  (void)kind;
  return "rx";
}
struct solar_os_mesh_subscription {
  unsigned mask;
};
solar_os_mesh_subscription_t *solar_os_mesh_subscribe(unsigned mask) {
  solar_os_mesh_subscription_t *s = malloc(sizeof(*s));
  s->mask = mask;
  return s;
}
unsigned solar_os_mesh_wait(solar_os_mesh_subscription_t *s, uint32_t timeout) {
  (void)timeout;
  return s ? s->mask : 0;
}
void solar_os_mesh_unsubscribe(solar_os_mesh_subscription_t *s) { free(s); }
static void key(const solar_os_app_t *app, solar_os_context_t *ctx, char ch) {
  solar_os_event_t e = {.type = SOLAR_OS_EVENT_CHAR, .data.ch = ch};
  assert(app->event(ctx, &e));
}
static void tick(const solar_os_app_t *app, solar_os_context_t *ctx) {
  solar_os_event_t e = {.type = SOLAR_OS_EVENT_TICK, .data.tick_ms = 500};
  assert(app->event(ctx, &e));
}
static bool contains(const char *text) {
  for (unsigned i = 0; i < rows; ++i)
    if (strstr(screen[i], text))
      return true;
  return false;
}
int main(void) {
  solar_os_context_t chat = {.argc = 2}, monitor = {.argc = 2};
  strcpy(chat.argv[0], "chat");
  strcpy(chat.argv[1], "slot0");
  strcpy(monitor.argv[0], "meshmon");
  strcpy(monitor.argv[1], "slot1");
  const solar_os_app_t *a = &solar_os_mesh_chat_app,
                       *b = &solar_os_mesh_monitor_app;
  assert(a->state_slot != b->state_slot);
  *a->state_slot = calloc(1, a->state_size);
  *b->state_slot = calloc(1, b->state_size);
  assert(a->start(&chat) == ESP_OK);
  key(a, &chat, 'h');
  key(a, &chat, 'i');
  assert(b->start(&monitor) == ESP_OK);
  tick(b, &monitor);
  assert(contains("RSSI -99 SNR --"));
  key(b, &monitor, 'x');
  key(a, &chat, '\r');
  assert(!strcmp(sent, "hi") && sent_slot == 0 && sent_channel == 0);
  tick(a, &chat);
  assert(contains("END_OF_LONG_MESSAGE"));
  key(a, &chat, '\t');
  key(a, &chat, 'o');
  key(a, &chat, 'k');
  key(a, &chat, '\r');
  assert(sent_channel == 1 && !strcmp(sent, "ok"));
  rows = 5;
  tick(b, &monitor);
  key(b, &monitor, SOLAR_OS_KEY_ESCAPE);
  assert(monitor.exit_requested);
  b->stop(&monitor);
  free(*b->state_slot);
  *b->state_slot = NULL;
  key(a, &chat, SOLAR_OS_KEY_ESCAPE);
  a->stop(&chat);
  free(*a->state_slot);
  *a->state_slot = NULL;
  puts("mesh view tests passed (independent chat/monitor state, wrapping, "
       "channel send, exit)");
}
