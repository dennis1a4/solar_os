#include "solar_os_keys.h"
#include "solar_os_pda_tools.h"
#include "solar_os_tui_widgets.h"
#include "solar_os_units.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
  solar_os_tui_t tui;
  bool suspended;
  unsigned field, unit, category;
  char text[3][64], result[128], message[128];
  solar_os_tui_input_state_t edit[3];
} convert_t;
static void *state;
#define app (*(convert_t *)state)
static void convert(void) {
  char *end;
  errno = 0;
  double value = strtod(app.text[0], &end), result;
  if (errno || !*app.text[0] || *end || !isfinite(value)) {
    strcpy(app.message, "Enter a finite number");
    return;
  }
  if (solar_units_convert(value, app.text[1], app.text[2], &result, app.message,
                          sizeof(app.message)))
    snprintf(app.result, sizeof(app.result), "%.12g %.16s = %.12g %.16s", value,
             app.text[1], result, app.text[2]);
  else
    app.result[0] = 0;
}
static void render(void) {
  if (app.suspended)
    return;
  unsigned rows = solar_os_tui_rows(&app.tui),
           cols = solar_os_tui_cols(&app.tui);
  solar_os_tui_clear(&app.tui);
  solar_os_tui_write_cell(&app.tui, 0, 0, cols,
                          "Unit converter (case-sensitive symbols)", 1);
  const char *labels[] = {"Value: ", "From: ", "To: "};
  for (unsigned i = 0; i < 3 && i + 1 < rows; i++)
    solar_os_tui_draw_input(&app.tui, i + 1, 0, cols, labels[i], app.text[i],
                            &app.edit[i],
                            app.field == i ? SOLAR_OS_TUI_ATTR_INVERSE : 0);
  solar_os_tui_write_cell(&app.tui, 4, 0, cols, app.result, 1);
  solar_os_tui_write_cell(&app.tui, 5, 0, cols, app.message, 0);
  if (rows > 10) {
    const solar_unit_t *selected = solar_units_get(app.unit);
    unsigned row = 6, position = 0, ordinal = 0, visible = rows - 9;
    for (size_t i = 0; i < app.unit; i++)
      if (!strcmp(solar_units_get(i)->category, selected->category))
        position++;
    unsigned first = position >= visible ? position - visible + 1 : 0;
    for (size_t i = 0; i < solar_units_count() && row + 3 < rows; i++) {
      const solar_unit_t *u = solar_units_get(i);
      if (strcmp(u->category, selected->category))
        continue;
      if (ordinal++ < first)
        continue;
      char text[96];
      snprintf(text, sizeof(text), "%-12s %s", u->symbol, u->category);
      solar_os_tui_write_cell(&app.tui, row++, 0, cols, text,
                              i == app.unit ? SOLAR_OS_TUI_ATTR_INVERSE : 0);
    }
  }
  if (rows > 2)
    solar_os_tui_write_cell(&app.tui, rows - 2, 0, cols,
                            "Tab field Enter convert F2 swap F3/F4 choose unit",
                            0);
  if (rows)
    solar_os_tui_write_cell(&app.tui, rows - 1, 0, cols,
                            "Up/Down unit [ ] category Ctrl+U clear Esc exit",
                            0);
  solar_os_tui_refresh(&app.tui);
}
static esp_err_t start(solar_os_context_t *ctx) {
  strcpy(app.text[0], "1");
  strcpy(app.text[1], "m");
  strcpy(app.text[2], "ft");
  if (solar_os_context_argc(ctx) != 1 && solar_os_context_argc(ctx) != 4) {
    solar_os_context_finish(ctx, 2, "convert [VALUE FROM TO]");
    return ESP_OK;
  }
  if (solar_os_context_argc(ctx) == 4)
    for (unsigned i = 0; i < 3; i++)
      snprintf(app.text[i], 64, "%.63s", solar_os_context_argv(ctx, i + 1));
  for (unsigned i = 0; i < 3; i++)
    app.edit[i].cursor = strlen(app.text[i]);
  esp_err_t err = solar_os_tui_screen_begin(&app.tui, ctx);
  if (err != ESP_OK)
    return err;
  solar_os_tui_enable_diff(&app.tui, true);
  convert();
  render();
  return ESP_OK;
}
static void stop(solar_os_context_t *ctx) {
  (void)ctx;
  solar_os_tui_end(&app.tui);
}
static void suspend_ui(solar_os_context_t *ctx) {
  (void)ctx;
  app.suspended = true;
}
static void resume_ui(solar_os_context_t *ctx) {
  (void)ctx;
  app.suspended = false;
  solar_os_tui_attach_session(&app.tui);
  render();
}
static bool event(solar_os_context_t *ctx, const solar_os_event_t *e) {
  if (e->type == SOLAR_OS_EVENT_TICK) {
    render();
    return true;
  }
  if (e->type != SOLAR_OS_EVENT_CHAR || app.suspended)
    return false;
  unsigned char c = e->data.ch;
  if (c == 27 || c == SOLAR_OS_KEY_APP_EXIT) {
    solar_os_context_finish(ctx, 0, NULL);
    return true;
  }
  if (c == '\t') {
    app.field = (app.field + 1) % 3;
    return true;
  }
  if (c == '\r' || c == '\n') {
    convert();
    return true;
  }
  if (c == 21) {
    app.text[app.field][0] = 0;
    app.edit[app.field] = (solar_os_tui_input_state_t){0};
    return true;
  }
  if (c == SOLAR_OS_KEY_F2) {
    char value[64];
    strcpy(value, app.text[1]);
    strcpy(app.text[1], app.text[2]);
    strcpy(app.text[2], value);
    convert();
    return true;
  }
  if (c == SOLAR_OS_KEY_F3 || c == SOLAR_OS_KEY_F4) {
    unsigned field = c == SOLAR_OS_KEY_F3 ? 1 : 2;
    snprintf(app.text[field], 64, "%s", solar_units_get(app.unit)->symbol);
    app.edit[field].cursor = strlen(app.text[field]);
    convert();
    return true;
  }
  if (c == SOLAR_OS_KEY_UP) {
    if (app.unit)
      app.unit--;
    return true;
  }
  if (c == SOLAR_OS_KEY_DOWN) {
    app.unit = (app.unit + 1) % solar_units_count();
    return true;
  }
  if (c == '[' || c == ']') {
    const char *category = solar_units_get(app.unit)->category;
    size_t n = solar_units_count();
    for (size_t i = 0; i < n; i++) {
      app.unit = c == ']' ? (app.unit + 1) % n : (app.unit + n - 1) % n;
      if (strcmp(category, solar_units_get(app.unit)->category))
        break;
    }
    return true;
  }
  solar_os_tui_input_key(app.text[app.field], 64, &app.edit[app.field], c,
                         solar_os_tui_cols(&app.tui));
  return true;
}
const solar_os_app_t solar_os_convert_app = {
    .name = "convert",
    .summary = "engineering and everyday unit conversions",
    .app_class = SOLAR_OS_APP_CLASS_TUI,
    .flags = SOLAR_OS_APP_FLAG_RESUMABLE,
    .start = start,
    .stop = stop,
    .suspend = suspend_ui,
    .resume = resume_ui,
    .event = event,
    .state_slot = &state,
    .state_size = sizeof(convert_t),
    .state_storage = SOLAR_OS_APP_STATE_EXTERNAL_REQUIRED,
    .tick_interval_ms = 200};
