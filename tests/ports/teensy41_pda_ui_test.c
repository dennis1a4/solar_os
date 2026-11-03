#include "esp_timer.h"
#include "solar_os_keys.h"
#include "solar_os_manual.h"
#include "solar_os_memory.h"
#include "solar_os_organizer.h"
#include "solar_os_organizer_apps.h"
#include "solar_os_pda_tools.h"
#include "solar_os_puzzle_apps.h"
#include "solar_os_tui_widgets.h"
#include "solar_os_writer_files.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static pim_db_t *database;
static unsigned locked, commits, draws, rows = 30, cols = 100;
static uint32_t clock_ms = 10000;
static uint8_t nes;
static const void *nes_owner;
static char screen[40][256], root[128];
const solar_os_app_t solar_os_reader_app = {.name = "reader"};
void *solar_os_memory_alloc(size_t n, solar_os_memory_class_t c,
                            const char *t) {
  (void)c;
  (void)t;
  return malloc(n);
}
void solar_os_memory_free(void *p) { free(p); }
void solar_os_organizer_begin(void) {}
pim_db_t *solar_os_organizer_lock(char *s, size_t n) {
  assert(!locked++);
  if (s)
    snprintf(s, n, "Saved locally");
  return database;
}
void solar_os_organizer_unlock(void) { assert(locked-- == 1); }
pim_db_t *solar_os_organizer_copy(void) {
  pim_db_t *d = malloc(sizeof(*d));
  assert(d);
  *d = *database;
  return d;
}
bool solar_os_organizer_commit(pim_db_t *d, uint32_t generation, char *s,
                               size_t n) {
  assert(!locked);
  if (generation != database->generation || !pim_valid(d)) {
    snprintf(s, n, "Invalid");
    return false;
  }
  d->generation++;
  *database = *d;
  commits++;
  snprintf(s, n, "Saved");
  return true;
}
bool solar_os_organizer_transfer(const char *p, unsigned k, bool i, char *s,
                                 size_t n) {
  (void)p;
  (void)k;
  (void)i;
  snprintf(s, n, "Transferred");
  return true;
}
bool solar_os_organizer_backup(const char *p, bool r, char *s, size_t n) {
  (void)p;
  (void)r;
  snprintf(s, n, "Backup");
  return true;
}
void solar_os_organizer_ack(void) {}
int32_t solar_os_organizer_today(uint16_t *m) {
  if (m)
    *m = 600;
  return pim_day(2026, 10, 26);
}
bool solar_os_puzzle_nes_claim(const void *o, bool on) {
  if (on) {
    if (nes_owner && nes_owner != o)
      return false;
    nes_owner = o;
  } else {
    assert(nes_owner == o);
    nes_owner = NULL;
  }
  return true;
}
uint8_t solar_os_puzzle_nes_read(const void *o) {
  return o == nes_owner ? nes : 0;
}
int64_t esp_timer_get_time(void) { return (int64_t)clock_ms * 1000; }
esp_err_t solar_os_storage_default_path(const char *leaf, char *out, size_t n) {
  return snprintf(out, n, "%s/%s", root, leaf) < (int)n ? ESP_OK
                                                        : ESP_ERR_INVALID_SIZE;
}
esp_err_t solar_os_storage_resolve_path(const char *p, char *out, size_t n) {
  return snprintf(out, n, "%s", p) < (int)n ? ESP_OK : ESP_ERR_INVALID_SIZE;
}
size_t solar_os_manual_count(void) { return 1; }
const solar_os_manual_page_t *solar_os_manual_get(size_t i) {
  static const solar_os_manual_page_t p = {.id = "test",
                                           .title = "Test manual"};
  return i ? NULL : &p;
}
int solar_os_context_argc(const solar_os_context_t *c) { return c->argc; }
const char *solar_os_context_argv(const solar_os_context_t *c, int i) {
  return c->argv[i];
}
void solar_os_context_finish(solar_os_context_t *c, int code, const char *s) {
  c->exit_requested = true;
  c->exit_code = code;
  if (s)
    snprintf(c->status_message, sizeof(c->status_message), "%s", s);
}
esp_err_t solar_os_context_request_launch_ex(solar_os_context_t *c,
                                             const solar_os_app_t *a, int argc,
                                             char **argv,
                                             solar_os_launch_policy_t p) {
  (void)p;
  assert(argc == 2 && argv[1][0]);
  c->requested_app = a;
  return ESP_OK;
}
esp_err_t solar_os_tui_screen_begin(solar_os_tui_t *t, solar_os_context_t *c) {
  (void)t;
  (void)c;
  return ESP_OK;
}
esp_err_t solar_os_tui_enable_diff(solar_os_tui_t *t, bool e) {
  (void)t;
  (void)e;
  return ESP_OK;
}
void solar_os_tui_attach_session(solar_os_tui_t *t) { (void)t; }
void solar_os_tui_end(solar_os_tui_t *t) { (void)t; }
size_t solar_os_tui_rows(const solar_os_tui_t *t) {
  (void)t;
  return rows;
}
size_t solar_os_tui_cols(const solar_os_tui_t *t) {
  (void)t;
  return cols;
}
void solar_os_tui_clear(solar_os_tui_t *t) {
  (void)t;
  memset(screen, 0, sizeof(screen));
}
void solar_os_tui_refresh(solar_os_tui_t *t) {
  (void)t;
  draws++;
}
esp_err_t solar_os_tui_write_cell(solar_os_tui_t *t, size_t r, size_t c,
                                  size_t n, const char *s, uint8_t a) {
  (void)t;
  (void)a;
  assert(r < rows && r < 40 && c + n <= cols);
  snprintf(screen[r] + c, sizeof(screen[r]) - c, "%.*s", (int)n, s);
  return ESP_OK;
}
esp_err_t solar_os_tui_draw_input(solar_os_tui_t *t, size_t r, size_t c,
                                  size_t n, const char *label, const char *s,
                                  solar_os_tui_input_state_t *state,
                                  uint8_t a) {
  (void)state;
  char text[512];
  snprintf(text, sizeof(text), "%s%s", label, s);
  return solar_os_tui_write_cell(t, r, c, n, text, a);
}
solar_os_tui_input_action_t
solar_os_tui_input_key(char *s, size_t n, solar_os_tui_input_state_t *p,
                       uint32_t k, size_t v) {
  (void)v;
  size_t len = strlen(s);
  if (p->cursor > len)
    p->cursor = len;
  if (k == '\r' || k == '\n')
    return SOLAR_OS_TUI_INPUT_SUBMIT;
  if (k == 27)
    return SOLAR_OS_TUI_INPUT_CANCEL;
  if (k == 8 || k == 127) {
    if (p->cursor) {
      memmove(s + p->cursor - 1, s + p->cursor, len - p->cursor + 1);
      p->cursor--;
    }
  } else if (k == SOLAR_OS_KEY_LEFT) {
    if (p->cursor)
      p->cursor--;
  } else if (k == SOLAR_OS_KEY_RIGHT) {
    if (p->cursor < len)
      p->cursor++;
  } else if (k >= 32 && k < 127 && len + 1 < n) {
    memmove(s + p->cursor + 1, s + p->cursor, len - p->cursor + 1);
    s[p->cursor++] = k;
  }
  return SOLAR_OS_TUI_INPUT_CHANGED;
}
static void tick(const solar_os_app_t *a, solar_os_context_t *c) {
  clock_ms += 200;
  solar_os_event_t e = {.type = SOLAR_OS_EVENT_TICK, .data.tick_ms = clock_ms};
  assert(a->event(c, &e));
}
static void key(const solar_os_app_t *a, solar_os_context_t *c,
                unsigned char k) {
  solar_os_event_t e = {.type = SOLAR_OS_EVENT_CHAR, .data.ch = k};
  assert(a->event(c, &e));
}
static void type(const solar_os_app_t *a, solar_os_context_t *c,
                 const char *s) {
  while (*s)
    key(a, c, *s++);
}
static solar_os_context_t begin(const solar_os_app_t *a, const char *arg) {
  solar_os_context_t c = {.argc = arg ? 2 : 1};
  snprintf(c.argv[0], sizeof(c.argv[0]), "%s", a->name);
  if (arg)
    snprintf(c.argv[1], sizeof(c.argv[1]), "%s", arg);
  *a->state_slot = calloc(1, a->state_size);
  assert(*a->state_slot);
  assert(a->start(&c) == ESP_OK);
  return c;
}
static void end(const solar_os_app_t *a, solar_os_context_t *c) {
  a->stop(c);
  free(*a->state_slot);
  *a->state_slot = NULL;
}
int main(void) {
  char temp[] = "/tmp/pda-ui-XXXXXX";
  assert(mkdtemp(temp));
  strcpy(root, temp);
  database = malloc(sizeof(*database));
  assert(database);
  pim_init(database, "ui-test");
  const solar_os_app_t *a = &solar_os_people_app;
  solar_os_context_t c = begin(a, NULL);
  key(a, &c, 'a');
  type(a, &c, "Person");
  key(a, &c, 19);
  tick(a, &c);
  assert(database->contacts == 1 &&
         !strcmp(database->person[0].name, "Person"));
  key(a, &c, 'e');
  key(a, &c, 21);
  type(a, &c, "CANCELLED");
  key(a, &c, 27);
  assert(!strcmp(database->person[0].name, "Person"));
  end(a, &c);
  a = &solar_os_calendar_app;
  c = begin(a, NULL);
  key(a, &c, 'a');
  type(a, &c, "Appointment");
  key(a, &c, 19);
  tick(a, &c);
  assert(database->events == 1 &&
         !strcmp(database->event[0].title, "Appointment"));
  for (unsigned i = 0; i < 3; i++) {
    key(a, &c, '\t');
    tick(a, &c);
  }
  key(a, &c, '/');
  type(a, &c, "appointment");
  key(a, &c, '\r');
  tick(a, &c);
  unsigned before = draws;
  a->suspend(&c);
  tick(a, &c);
  assert(draws == before);
  a->resume(&c);
  assert(draws > before);
  end(a, &c);
  a = &solar_os_tasks_app;
  c = begin(a, NULL);
  key(a, &c, 'a');
  type(a, &c, "Check hardware");
  key(a, &c, 19);
  tick(a, &c);
  assert(database->tasks == 1);
  key(a, &c, ' ');
  tick(a, &c);
  assert(database->task[0].done);
  key(a, &c, '\t');
  tick(a, &c);
  key(a, &c, 'd');
  key(a, &c, 'y');
  tick(a, &c);
  assert(!database->tasks);
  end(a, &c);
  assert(commits == 5);
  a = &solar_os_convert_app;
  c = begin(a, NULL);
  assert(strstr(screen[4], "3.280839"));
  key(a, &c, SOLAR_OS_KEY_F2);
  tick(a, &c);
  assert(strstr(screen[4], "0.3048"));
  end(a, &c);
  char path[256];
  snprintf(path, sizeof(path), "%s/needle.md", root);
  FILE *f = fopen(path, "w");
  assert(f);
  fputs("# Reference\n", f);
  fclose(f);
  a = &solar_os_library_app;
  c = begin(a, root);
  key(a, &c, '/');
  type(a, &c, "needle");
  key(a, &c, '\r');
  key(a, &c, '\r');
  assert(c.requested_app == &solar_os_reader_app);
  end(a, &c);
  rows = 16;
  cols = 26;
  a = &solar_os_sudoku_app;
  c = begin(a, NULL);
  assert(nes_owner);
  nes = 16;
  tick(a, &c);
  assert(strstr(screen[1], "Col 2"));
  nes = 0;
  tick(a, &c);
  nes = 128;
  tick(a, &c);
  assert(strstr(screen[1], "Row 2"));
  nes = 0;
  tick(a, &c);
  a->suspend(&c);
  assert(!nes_owner);
  a->resume(&c);
  assert(nes_owner);
  end(a, &c);
  assert(!nes_owner);
  c = begin(a, NULL);
  assert(strstr(screen[rows - 2], "Saved game"));
  end(a, &c);
  a = &solar_os_mines_app;
  c = begin(a, NULL);
  key(a, &c, 'f');
  tick(a, &c);
  assert(strchr(screen[2], 'F'));
  key(a, &c, 'f');
  key(a, &c, ' ');
  tick(a, &c);
  assert(strchr(screen[2], '.'));
  key(a, &c, 'n');
  nes = 1;
  tick(a, &c);
  nes = 0;
  tick(a, &c);
  assert(!strstr(screen[rows - 2], "Start new"));
  end(a, &c);
  char cmd[512];
  snprintf(cmd, sizeof(cmd), "%s/.organizer/sudoku.bin", root);
  unlink(cmd);
  snprintf(cmd, sizeof(cmd), "%s/.organizer/mines.bin", root);
  unlink(cmd);
  snprintf(cmd, sizeof(cmd), "%s/.organizer", root);
  rmdir(cmd);
  unlink(path);
  rmdir(root);
  free(database);
  puts("PDA UI: add/edit/cancel/complete/delete, views/search, suspend, "
       "converter, library child launch, small layouts, NES mapping and puzzle "
       "resume passed");
}
