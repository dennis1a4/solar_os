#if SK_MESH_CLIENT
#include "solar_os_mesh_view.h"
#include "solar_os_keys.h"
#include "solar_os_mesh_client.h"
#include "solar_os_mesh_service.h"
#include "solar_os_tui.h"
#include "solar_os_tui_widgets.h"
#include <stdio.h>
#include <string.h>
typedef struct {
  solar_os_tui_t tui;
  solar_os_mesh_subscription_t *subscription;
  unsigned slot, channel, count, head;
  uint64_t cursor, lost;
  bool monitor, opened;
  uint32_t rendered;
  char input[SOLAR_MESH_TEXT + 1], notice[96];
  solar_mesh_event_t history[24];
} mesh_view_t;
static void *chat_state, *monitor_state;
#define view (*state)
static void line(mesh_view_t *state, size_t row, const char *text,
                 uint8_t attr) {
  solar_os_tui_write_cell(&view.tui, row, 0, solar_os_tui_cols(&view.tui), text,
                          attr);
}
static void render(mesh_view_t *state) {
  char text[384];
  solar_mesh_status_t s = {0};
  bool connected = solar_os_mesh_service_status(view.slot, &s);
  size_t rows = solar_os_tui_rows(&view.tui);
  solar_os_tui_clear(&view.tui);
  snprintf(text, sizeof(text), "%s slot%u / %s / channel %u / %s",
           view.monitor ? "Mesh monitor" : "Mesh chat", view.slot, s.driver,
           view.channel,
           !connected   ? "disconnected"
           : s.ready    ? "ready"
           : s.error[0] ? "failed"
                        : "connecting");
  line(state, 0, text, SOLAR_OS_TUI_ATTR_BOLD);
  if (rows < 6) {
    line(state, 1, "Resize terminal. Esc exits.", 0);
    solar_os_tui_refresh(&view.tui);
    return;
  }
  size_t width = solar_os_tui_cols(&view.tui);
  if (!width)
    width = 1;
  if (width > 160)
    width = 160;
  size_t total = 0, skip = 0, row = 1;
  // Two bounded passes wrap long messages while keeping the newest lines
  // visible.
  for (unsigned pass = 0; pass < 2; ++pass) {
    for (unsigned i = 0; i < view.count; ++i) {
      const solar_mesh_event_t *e =
          &view.history[(view.head + 24 - view.count + i) % 24];
      char metrics[64] = "";
      if (view.monitor) {
        char rssi[20] = "--", snr[20] = "--";
        if (e->has_rssi)
          snprintf(rssi, sizeof(rssi), "%d", e->rssi);
        if (e->has_snr)
          snprintf(snr, sizeof(snr), "%.2f", e->snr_q4 / 4.0);
        snprintf(metrics, sizeof(metrics), " RSSI %s SNR %s", rssi, snr);
      }
      snprintf(text, sizeof(text), "%s %s%u %.12s%s: %s",
               solar_mesh_event_name(e->kind), e->direct ? "DM/" : "ch",
               e->channel, e->sender, metrics, e->text);
      size_t len = strlen(text), count = (len + width - 1) / width;
      if (!count)
        count = 1;
      if (!pass) {
        total += count;
        continue;
      }
      for (size_t j = 0; j < count; ++j) {
        if (skip) {
          --skip;
          continue;
        }
        char segment[161];
        size_t off = j * width, n = len > off ? len - off : 0;
        if (n > width)
          n = width;
        memcpy(segment, text + off, n);
        segment[n] = 0;
        line(state, row++, segment, 0);
      }
    }
    if (!pass)
      skip = total > rows - 5 ? total - (rows - 5) : 0;
  }
  snprintf(text, sizeof(text), "%s%s | missed history: %llu", view.notice,
           s.error, (unsigned long long)view.lost);
  line(state, rows - 3, text, 0);
  size_t input_length = strlen(view.input),
         input_width = width > 3 ? width - 3 : 1;
  snprintf(text, sizeof(text), "> %s",
           view.input +
               (input_length > input_width ? input_length - input_width : 0));
  line(state, rows - 2, view.monitor ? "Read-only packet monitor" : text, 0);
  line(state, rows - 1,
       view.monitor ? "Esc exits; radio and logging keep running"
                    : "Enter sends to channel; Tab changes channel; Esc exits",
       SOLAR_OS_TUI_ATTR_BOLD);
  solar_os_tui_refresh(&view.tui);
}
static esp_err_t start(solar_os_context_t *ctx, mesh_view_t *state) {
  if (ctx->argc < 2 || strlen(ctx->argv[1]) != 5 ||
      strncmp(ctx->argv[1], "slot", 4) || ctx->argv[1][4] < '0' ||
      ctx->argv[1][4] > '2')
    goto usage;
  view.slot = ctx->argv[1][4] - '0';
  view.monitor = !strcmp(ctx->argv[0], "meshmon");
  if (ctx->argc > 2) {
    if (view.monitor || strlen(ctx->argv[2]) != 1 || *ctx->argv[2] < '0' ||
        *ctx->argv[2] > '7')
      goto usage;
    view.channel = *ctx->argv[2] - '0';
  }
  {
    esp_err_t err = solar_os_tui_screen_begin(&view.tui, ctx);
    if (err != ESP_OK)
      return err;
  }
  view.opened = true;
  view.subscription = solar_os_mesh_subscribe(1U << view.slot);
  if (!view.subscription) {
    solar_os_context_finish(ctx, 1,
                            "No mesh notification subscription available");
    return ESP_OK;
  }
  render(state);
  return ESP_OK;
usage:
  solar_os_context_finish(ctx, 2,
                          "usage: chat slotN [CHANNEL] | meshmon slotN");
  return ESP_OK;
}
static void stop(solar_os_context_t *ctx, mesh_view_t *state) {
  (void)ctx;
  solar_os_mesh_unsubscribe(view.subscription);
  view.subscription = NULL;
  if (view.opened)
    solar_os_tui_end(&view.tui);
}
static bool event(solar_os_context_t *ctx, const solar_os_event_t *event,
                  mesh_view_t *state) {
  if (event->type == SOLAR_OS_EVENT_TICK) {
    solar_mesh_event_t e;
    unsigned notified = solar_os_mesh_wait(view.subscription, 0);
    for (unsigned i = 0;
         notified && i < 32 &&
         solar_os_mesh_service_event(view.slot, &view.cursor, &e, &view.lost);
         ++i) {
      if (!view.monitor && (e.kind == SOLAR_MESH_PACKET ||
                            (e.kind == SOLAR_MESH_MESSAGE && !e.direct &&
                             e.channel != view.channel)))
        continue;
      view.history[view.head] = e;
      view.head = (view.head + 1) % 24;
      if (view.count < 24)
        ++view.count;
    }
    if ((uint32_t)(event->data.tick_ms - view.rendered) >= 250) {
      view.rendered = event->data.tick_ms;
      render(state);
    }
    return true;
  }
  if (event->type != SOLAR_OS_EVENT_CHAR)
    return false;
  unsigned char key = event->data.ch;
  if (key == SOLAR_OS_KEY_ESCAPE || key == SOLAR_OS_KEY_APP_EXIT) {
    solar_os_context_finish(ctx, 0, NULL);
    return true;
  }
  if (view.monitor)
    return true;
  size_t n = strlen(view.input);
  if (key == '\r' || key == '\n') {
    if (n) {
      solar_mesh_result_t r =
          solar_os_mesh_service_send(view.slot, view.channel, NULL, view.input);
      snprintf(view.notice, sizeof(view.notice), "%s",
               solar_mesh_result_name(r));
      if (r == SOLAR_MESH_OK)
        view.input[0] = 0;
    }
  } else if (key == '\t') {
    view.channel = (view.channel + 1) % SOLAR_MESH_CHANNELS;
    view.count = view.head = 0;
  } else if (key == 8 || key == 127) {
    if (n)
      view.input[n - 1] = 0;
  } else if (key >= 32 && key < 127 && n < SOLAR_MESH_TEXT) {
    view.input[n] = key;
    view.input[n + 1] = 0;
  }
  render(state);
  return true;
}
static esp_err_t chat_start(solar_os_context_t *ctx) {
  return start(ctx, (mesh_view_t *)chat_state);
}
static void chat_stop(solar_os_context_t *ctx) {
  stop(ctx, (mesh_view_t *)chat_state);
}
static bool chat_event(solar_os_context_t *ctx, const solar_os_event_t *e) {
  return event(ctx, e, (mesh_view_t *)chat_state);
}
static esp_err_t monitor_start(solar_os_context_t *ctx) {
  return start(ctx, (mesh_view_t *)monitor_state);
}
static void monitor_stop(solar_os_context_t *ctx) {
  stop(ctx, (mesh_view_t *)monitor_state);
}
static bool monitor_event(solar_os_context_t *ctx, const solar_os_event_t *e) {
  return event(ctx, e, (mesh_view_t *)monitor_state);
}
const solar_os_app_t solar_os_mesh_chat_app = {
    .name = "chat",
    .summary = "Mesh companion channel chat",
    .app_class = SOLAR_OS_APP_CLASS_TUI,
    .start = chat_start,
    .stop = chat_stop,
    .event = chat_event,
    .tick_interval_ms = 100,
    .state_slot = &chat_state,
    .state_size = sizeof(mesh_view_t),
    .state_storage = SOLAR_OS_APP_STATE_EXTERNAL_REQUIRED,
};
const solar_os_app_t solar_os_mesh_monitor_app = {
    .name = "meshmon",
    .summary = "Mesh companion packet monitor",
    .app_class = SOLAR_OS_APP_CLASS_TUI,
    .start = monitor_start,
    .stop = monitor_stop,
    .event = monitor_event,
    .tick_interval_ms = 100,
    .state_slot = &monitor_state,
    .state_size = sizeof(mesh_view_t),
    .state_storage = SOLAR_OS_APP_STATE_EXTERNAL_REQUIRED,
};

#endif
