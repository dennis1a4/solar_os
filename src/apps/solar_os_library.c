#include "solar_os_keys.h"
#include "solar_os_manual.h"
#include "solar_os_pda_tools.h"
#include "solar_os_reader.h"
#include "solar_os_storage.h"
#include "solar_os_tui_widgets.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#define LIBRARY_CAP 512
typedef struct {
  char path[SOLAR_OS_APP_ARG_LEN], title[96];
  bool manual;
} book_t;
typedef struct {
  solar_os_tui_t tui;
  book_t book[LIBRARY_CAP];
  uint16_t visible[LIBRARY_CAP];
  unsigned count, found, cursor, top, mode, dirs;
  bool suspended, searching, limited;
  char query[97], root[SOLAR_OS_STORAGE_PATH_MAX], message[128];
  solar_os_tui_input_state_t edit;
} library_t;
static void *state;
#define app (*(library_t *)state)
static bool supported(const char *name) {
  const char *s = strrchr(name, '.');
  return s && (!strcasecmp(s, ".md") || !strcasecmp(s, ".markdown") ||
               !strcasecmp(s, ".txt") || !strcasecmp(s, ".text") ||
               !strcasecmp(s, ".epub"));
}
static void add(const char *path, const char *title, bool manual) {
  if (app.count == LIBRARY_CAP || strlen(path) >= sizeof(app.book[0].path)) {
    app.limited = true;
    return;
  }
  for (unsigned i = 0; i < app.count; i++)
    if (!strcmp(app.book[i].path, path))
      return;
  book_t *b = &app.book[app.count++];
  snprintf(b->path, sizeof(b->path), "%s", path);
  snprintf(b->title, sizeof(b->title), "%.95s", title);
  b->manual = manual;
}
static void scan(const char *path, unsigned depth) {
  if (depth > 4 || app.dirs++ >= 64) {
    app.limited = true;
    return;
  }
  DIR *dir = opendir(path);
  if (!dir)
    return;
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (entry->d_name[0] == '.')
      continue;
    char full[SOLAR_OS_STORAGE_PATH_MAX];
    int n = snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
    if (n < 0 || (size_t)n >= sizeof(full)) {
      app.limited = true;
      continue;
    }
    struct stat info;
    if (stat(full, &info))
      continue;
    if (S_ISDIR(info.st_mode))
      scan(full, depth + 1);
    else if (S_ISREG(info.st_mode) && supported(entry->d_name))
      add(full, entry->d_name, false);
    if (app.count == LIBRARY_CAP)
      break;
  }
  closedir(dir);
}
static void filter(void) {
  app.found = 0;
  for (unsigned i = 0; i < app.count; i++) {
    book_t *b = &app.book[i];
    if ((app.mode == 1 && b->manual) || (app.mode == 2 && !b->manual))
      continue; /* shared search keeps catalog and organizer behavior alike */
    bool match = !*app.query;
    for (const char *p = b->title; !match && *p; p++)
      if (!strncasecmp(p, app.query, strlen(app.query)))
        match = true;
    for (const char *p = b->path; !match && *p; p++)
      if (!strncasecmp(p, app.query, strlen(app.query)))
        match = true;
    if (match)
      app.visible[app.found++] = i;
  }
  if (app.cursor >= app.found)
    app.cursor = app.found ? app.found - 1 : 0;
}
static void refresh(void) {
  app.count = app.dirs = 0;
  app.limited = false;
  if (*app.root)
    scan(app.root, 0);
  else {
    const char *roots[] = {"/sd/Library", "/sd/books", "/sd/Documents",
                           "/flash/Library", "/usb/Library"};
    for (unsigned i = 0; i < sizeof(roots) / sizeof(roots[0]); i++)
      scan(roots[i], 0);
  }
  for (size_t i = 0; i < solar_os_manual_count(); i++) {
    const solar_os_manual_page_t *p = solar_os_manual_get(i);
    char path[SOLAR_OS_APP_ARG_LEN];
    snprintf(path, sizeof(path), "man:%s", p->id);
    add(path, p->title, true);
  }
  for (unsigned i = 1; i < app.count; i++) {
    book_t b = app.book[i];
    unsigned j = i;
    while (j && strcasecmp(app.book[j - 1].title, b.title) > 0) {
      app.book[j] = app.book[j - 1];
      j--;
    }
    app.book[j] = b;
  }
  filter();
  snprintf(app.message, sizeof(app.message),
           app.limited ? "Catalog bounded: 512 entries, 64 folders, depth 4; "
                         "use a narrower root"
                       : "%u documents/manuals indexed",
           app.count);
}
static void render(void) {
  if (app.suspended)
    return;
  unsigned rows = solar_os_tui_rows(&app.tui),
           cols = solar_os_tui_cols(&app.tui);
  if (rows < 6)
    return;
  solar_os_tui_clear(&app.tui);
  char text[256];
  snprintf(text, sizeof(text), "Reference library: %u matches (%s)", app.found,
           app.mode == 0   ? "all"
           : app.mode == 1 ? "documents"
                           : "manuals");
  solar_os_tui_write_cell(&app.tui, 0, 0, cols, text, 1);
  solar_os_tui_write_cell(
      &app.tui, 1, 0, cols,
      *app.root
          ? app.root
          : "/sd/{Library,books,Documents} + /flash/Library + /usb/Library",
      0);
  unsigned visible = rows - 5;
  if (app.cursor < app.top)
    app.top = app.cursor;
  if (app.cursor >= app.top + visible)
    app.top = app.cursor - visible + 1;
  for (unsigned i = 0; i < visible && app.top + i < app.found; i++) {
    book_t *b = &app.book[app.visible[app.top + i]];
    snprintf(text, sizeof(text), "%s %s", b->manual ? "[Manual]" : "[Book]",
             b->title);
    solar_os_tui_write_cell(
        &app.tui, i + 2, 0, cols, text,
        app.top + i == app.cursor ? SOLAR_OS_TUI_ATTR_INVERSE : 0);
  }
  if (app.found)
    solar_os_tui_write_cell(&app.tui, rows - 3, 0, cols,
                            app.book[app.visible[app.cursor]].path, 0);
  else
    solar_os_tui_write_cell(
        &app.tui, rows - 3, 0, cols,
        "Copy .txt/.md/.epub documents into a library folder", 0);
  if (app.searching)
    solar_os_tui_draw_input(&app.tui, rows - 2, 0, cols, "Search: ", app.query,
                            &app.edit, 0);
  else
    solar_os_tui_write_cell(&app.tui, rows - 2, 0, cols, app.message, 0);
  solar_os_tui_write_cell(&app.tui, rows - 1, 0, cols,
                          "Enter read / search Tab filter r refresh q exit", 0);
  solar_os_tui_refresh(&app.tui);
}
static esp_err_t start(solar_os_context_t *ctx) {
  if (solar_os_context_argc(ctx) > 1) {
    if (solar_os_storage_resolve_path(solar_os_context_argv(ctx, 1), app.root,
                                      sizeof(app.root)) != ESP_OK)
      return ESP_ERR_INVALID_ARG;
  }
  esp_err_t err = solar_os_tui_screen_begin(&app.tui, ctx);
  if (err != ESP_OK)
    return err;
  solar_os_tui_enable_diff(&app.tui, true);
  refresh();
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
  unsigned char key = e->data.ch;
  if (key == SOLAR_OS_KEY_APP_EXIT) {
    solar_os_context_finish(ctx, 0, NULL);
    return true;
  }
  if (app.searching) {
    solar_os_tui_input_action_t action =
        solar_os_tui_input_key(app.query, sizeof(app.query), &app.edit, key,
                               solar_os_tui_cols(&app.tui));
    filter();
    if (action == SOLAR_OS_TUI_INPUT_SUBMIT ||
        action == SOLAR_OS_TUI_INPUT_CANCEL)
      app.searching = false;
    return true;
  }
  if (key == 'q' || key == 27)
    solar_os_context_finish(ctx, 0, NULL);
  else if (key == '/') {
    app.searching = true;
    app.edit.cursor = strlen(app.query);
  } else if (key == '\t') {
    app.mode = (app.mode + 1) % 3;
    app.cursor = app.top = 0;
    filter();
  } else if (key == 'r')
    refresh();
  else if (key == SOLAR_OS_KEY_UP && app.cursor)
    app.cursor--;
  else if (key == SOLAR_OS_KEY_DOWN && app.cursor + 1 < app.found)
    app.cursor++;
  else if (key == SOLAR_OS_KEY_HOME)
    app.cursor = 0;
  else if (key == SOLAR_OS_KEY_END && app.found)
    app.cursor = app.found - 1;
  else if ((key == '\r' || key == '\n') && app.found) {
    char *argv[] = {"reader", app.book[app.visible[app.cursor]].path};
    esp_err_t err = solar_os_context_request_launch_ex(
        ctx, &solar_os_reader_app, 2, argv, SOLAR_OS_LAUNCH_CHILD_RETURN);
    if (err != ESP_OK)
      snprintf(app.message, sizeof(app.message),
               "Cannot open Reader (%d); main display required", err);
  }
  return true;
}
const solar_os_app_t solar_os_library_app = {
    .name = "library",
    .summary = "searchable offline documents and manuals",
    .app_class = SOLAR_OS_APP_CLASS_TUI,
    .flags = SOLAR_OS_APP_FLAG_RESUMABLE,
    .start = start,
    .stop = stop,
    .suspend = suspend_ui,
    .resume = resume_ui,
    .event = event,
    .state_slot = &state,
    .state_size = sizeof(library_t),
    .state_storage = SOLAR_OS_APP_STATE_EXTERNAL_REQUIRED,
    .tick_interval_ms = 200};
