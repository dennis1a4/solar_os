#include "solar_os_dnp3.h"
#include "solar_os_dnp3_app.h"
#include "solar_os_keys.h"
#include "solar_os_memory.h"
#include "solar_os_tui_widgets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
struct solar_os_dnp3 {
  dnp_lab_t lab;
  solar_os_dnp3_status_t status;
};
static solar_os_dnp3_t *service;
static unsigned locked, draws, destroyed, halted, cols = 100, rows = 30;
static char screen[40][201];
void *solar_os_memory_alloc(size_t n, solar_os_memory_class_t c,
                            const char *tag) {
  (void)c;
  (void)tag;
  return malloc(n);
}
void solar_os_memory_free(void *p) { free(p); }
esp_err_t solar_os_dnp3_create(solar_os_dnp3_t **s) {
  service = calloc(1, sizeof(*service));
  assert(service);
  dnp_lab_config_t c;
  dnp_lab_defaults(&c);
  dnp_lab_init(&service->lab, &c);
  *s = service;
  return ESP_OK;
}
void solar_os_dnp3_halt(solar_os_dnp3_t *s) {
  assert(!locked);
  s->status.running = false;
  halted++;
}
void solar_os_dnp3_destroy(solar_os_dnp3_t *s) {
  assert(!locked);
  free(s);
  service = NULL;
  destroyed++;
}
dnp_lab_t *solar_os_dnp3_lock(solar_os_dnp3_t *s, solar_os_dnp3_status_t *st) {
  assert(!locked++);
  if (st)
    *st = s->status;
  return &s->lab;
}
void solar_os_dnp3_unlock(solar_os_dnp3_t *s) {
  (void)s;
  assert(locked-- == 1);
}
bool solar_os_dnp3_command(solar_os_dnp3_t *s, const char *line, char *out,
                           size_t n) {
  assert(!locked);
  if (!strcmp(line, "start") || !strcmp(line, "stop")) {
    s->status.running = !strcmp(line, "start");
    snprintf(out, n, "OK");
    return true;
  }
  return dnp_lab_command(&s->lab, line, s->status.running, out, n);
}
int solar_os_context_argc(const solar_os_context_t *c) { return c->argc; }
const char *solar_os_context_argv(const solar_os_context_t *c, int i) {
  return c->argv[i];
}
void solar_os_context_finish(solar_os_context_t *c, int code,
                             const char *message) {
  c->exit_requested = true;
  c->exit_code = code;
  if (message)
    snprintf(c->status_message, sizeof(c->status_message), "%s", message);
}
esp_err_t solar_os_tui_screen_begin(solar_os_tui_t *t, solar_os_context_t *c) {
  (void)t;
  (void)c;
  return ESP_OK;
}
esp_err_t solar_os_tui_enable_diff(solar_os_tui_t *t, bool enabled) {
  (void)t;
  (void)enabled;
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
  assert(!locked);
  memset(screen, 0, sizeof(screen));
}
void solar_os_tui_refresh(solar_os_tui_t *t) {
  (void)t;
  draws++;
}
esp_err_t solar_os_tui_write_cell(solar_os_tui_t *t, size_t r, size_t c,
                                  size_t w, const char *text, uint8_t attr) {
  (void)t;
  (void)attr;
  assert(!locked && r < rows && c + w <= cols && r < 40);
  snprintf(screen[r], sizeof(screen[r]), "%.*s", (int)w, text);
  return ESP_OK;
}
static void key(solar_os_context_t *c, unsigned char k) {
  solar_os_event_t e = {.type = SOLAR_OS_EVENT_CHAR, .data.ch = k};
  assert(solar_os_dnp3_app.event(c, &e));
}
static void tick(solar_os_context_t *c);
static void command(solar_os_context_t *c, const char *s) {
  key(c, ':');
  while (*s)
    key(c, *s++);
  key(c, '\r');
  tick(c);
}
static void tick(solar_os_context_t *c) {
  solar_os_event_t e = {.type = SOLAR_OS_EVENT_TICK, .data.tick_ms = 100};
  assert(solar_os_dnp3_app.event(c, &e));
}
static void begin(solar_os_context_t *c) {
  *solar_os_dnp3_app.state_slot = calloc(1, solar_os_dnp3_app.state_size);
  assert(*solar_os_dnp3_app.state_slot);
  assert(solar_os_dnp3_app.start(c) == ESP_OK);
}
static void end(solar_os_context_t *c) {
  solar_os_dnp3_app.stop(c);
  free(*solar_os_dnp3_app.state_slot);
  *solar_os_dnp3_app.state_slot = NULL;
}
int main(void) {
  char dir[] = "/tmp/dnp3-app-XXXXXX";
  assert(mkdtemp(dir));
  char profile[128], log[128], line[224];
  snprintf(profile, sizeof(profile), "%s/profile", dir);
  snprintf(log, sizeof(log), "%s/traffic", dir);
  solar_os_context_t c = {.argc = 1};
  begin(&c);
  assert(!service->status.running && strstr(screen[0], "STOP"));
  command(&c, "mode demo");
  command(&c, "point ai 3 -123");
  command(&c, "rule add binary b2a 3 1");
  command(&c, "fault a2b duplicate 3");
  snprintf(line, sizeof(line), "save %s", profile);
  command(&c, line);
  command(&c, "point ai 3 999");
  command(&c, "rule clear");
  snprintf(line, sizeof(line), "load %s", profile);
  command(&c, line);
  assert(service->lab.points[3].analog == -123 &&
         service->lab.config.rules[0].enabled &&
         service->lab.config.faults[0].duplicate_every == 3);
  FILE *f = fopen(profile, "a");
  assert(f);
  fputs("point ai 3 555\nmode nonsense\n", f);
  fclose(f);
  command(&c, line);
  assert(service->lab.points[3].analog == -123); /* transactional failure */
  snprintf(line, sizeof(line), "log %s", log);
  command(&c, line);
  for (unsigned i = 0; i < 200; i++)
    dnp_lab_log(&service->lab, 0, "TEST", "traffic", NULL, 0);
  unsigned before = draws;
  solar_os_dnp3_app.suspend(&c);
  for (unsigned i = 0; i < 5; i++)
    tick(&c);
  assert(draws == before);
  solar_os_dnp3_app.resume(&c);
  assert(draws > before);
  command(&c, "log stop");
  f = fopen(log, "r");
  assert(f);
  bool lost = false;
  while (fgets(line, sizeof(line), f))
    if (strstr(line, "log_lost=72"))
      lost = true;
  fclose(f);
  assert(lost);
  command(&c, "start");
  assert(service->status.running);
  command(&c, "mode bridge");
  assert(service->lab.config.mode == DNP_LAB_DEMO);
  command(&c, "stop");
  key(&c, '\t');
  key(&c, '\r');
  key(&c, 27);
  for (unsigned i = 0; i < 5; i++) {
    key(&c, '\t');
    key(&c, SOLAR_OS_KEY_DOWN);
    key(&c, SOLAR_OS_KEY_UP);
  }
  cols = 26;
  rows = 16;
  for (unsigned i = 0; i < 5; i++) {
    key(&c, '\t');
    tick(&c);
  }
  command(&c, "point bi 0 1");
  key(&c, 'q');
  assert(c.exit_requested);
  end(&c);
  assert(halted && destroyed);
  c = (solar_os_context_t){.argc = 2};
  strcpy(c.argv[1], "--demo");
  begin(&c);
  assert(service->status.running && service->lab.config.mode == DNP_LAB_DEMO);
  end(&c);
  unlink(profile);
  unlink(log);
  rmdir(dir);
  puts("DNP3 app: stopped/demo, transactional profiles, rules/fault "
       "persistence, background logging/loss, small screen, lifecycle cleanup "
       "passed");
}
