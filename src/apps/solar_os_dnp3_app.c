#include "solar_os_dnp3_app.h"
#include "solar_os_dnp3.h"
#include "solar_os_keys.h"
#include "solar_os_memory.h"
#include "solar_os_tui_widgets.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The worker owns I/O; these snapshots keep display/file writes outside its
 * lock. */
typedef struct {
  solar_os_tui_t tui;
  solar_os_dnp3_t *service;
  bool suspended, frozen, input, hex;
  unsigned tab, scroll;
  char command[224], notice[128], lines[40][200];
  dnp_lab_event_t record;
  uint64_t view_end, log_next, log_lost;
  FILE *log;
} app_t;
static void *state;
#define app (*(app_t *)state)
static const char *modes[] = {"bridge", "master", "outstation", "demo"};
static const char *ports[] = {"uart7", "uart8", "uart3"};
static const char *help[] = {
    "s start/stop; Tab views; space freeze; x hex; : command; q exit",
    "mode bridge|master|outstation|demo (stop first)",
    "a serial uart7 9600 8n1    b serial uart8 9600 8n1",
    "a tcp listen 20000    b tcp connect 192.168.1.10 20000",
    "addresses 1 1024    interval 1000    timeout 2000    points 16",
    "start / stop; master uses B; outstation uses A; bridge uses both",
    "point bi|bo INDEX 0|1 [FLAGS]    point ai|ao INDEX VALUE [FLAGS]",
    "poll    control binary|analog INDEX VALUE [sbo|direct]",
    "rule add link a2b|b2a src|dst OLD NEW",
    "rule add control a2b|b2a OLD_INDEX NEW_INDEX",
    "rule add binary a2b|b2a INDEX 0|1 [FLAGS]",
    "rule add analog a2b|b2a INDEX VALUE [FLAGS]",
    "rule del ID    rule clear    fault clear",
    "fault a2b|b2a drop|duplicate|crc EVERY_N (0 disables)",
    "fault a2b|b2a delay MS    fault a2b|b2a truncate BYTES EVERY_N",
    "fault a2b|b2a flip BYTE_OFFSET MASK EVERY_N",
    "save /path    load /path (stopped)    log /NEWpath    log stop",
    "Edits: supported single-frame objects only; SKIP retains payload",
    "Simulator: static BI/AI/BO/AO, CROB latch, int32 AO, SBO/direct",
    "No secure auth, events, time sync or app-fragment simulation",
    "UARTs need external level transceivers; existing pin leases apply",
    "Ctrl+] closes; suspend keeps I/O and file logging active"};
static void endpoint_text(char *out, size_t n, char name,
                          const dnp_lab_endpoint_config_t *c) {
  const char *fmt = c->format == 0     ? "8n1"
                    : c->format == 2   ? "7e1"
                    : c->format == 3   ? "7o1"
                    : c->format == 6   ? "8e1"
                    : c->format == 7   ? "8o1"
                    : c->format == 256 ? "8n2"
                    : c->format == 258 ? "7e2"
                    : c->format == 259 ? "7o2"
                    : c->format == 262 ? "8e2"
                                       : "8o2";
  if (c->kind == DNP_LAB_SERIAL)
    snprintf(out, n, "%c serial %s %lu %s", name, ports[c->slot],
             (unsigned long)c->baud, fmt);
  else if (c->kind == DNP_LAB_LISTEN)
    snprintf(out, n, "%c tcp listen %u", name, c->port);
  else
    snprintf(out, n, "%c tcp connect %s %u", name, c->host, c->port);
}
static void log_pending(void) {
  if (!app.log || !app.service)
    return;
  for (unsigned i = 0; i < 32; i++) {
    dnp_lab_t *l = solar_os_dnp3_lock(app.service, NULL);
    uint64_t first =
        l->events > DNP_LAB_HISTORY ? l->events - DNP_LAB_HISTORY + 1 : 1;
    if (app.log_next < first) {
      app.log_lost += first - app.log_next;
      app.log_next = first;
    }
    bool have = app.log_next <= l->events;
    if (have)
      app.record = l->history[(app.log_next - 1) % DNP_LAB_HISTORY];
    solar_os_dnp3_unlock(app.service);
    if (!have)
      break;
    dnp_lab_event_t *e = &app.record;
    int ok = fprintf(app.log, "%" PRIu64 "\t%lu\t%s\t%s\t%s\t", e->sequence,
                     (unsigned long)e->at, e->direction ? "B>A" : "A>B",
                     e->stage, e->text);
    for (unsigned j = 0; j < e->length; j++)
      fprintf(app.log, "%02x", e->data[j]);
    fputc('\n', app.log);
    app.log_next++;
    if (ok < 0 || ferror(app.log)) {
      snprintf(app.notice, sizeof(app.notice),
               "Log write failed; capture continues");
      fclose(app.log);
      app.log = NULL;
      break;
    }
  }
}
static bool profile(const char *path, bool load) {
  solar_os_dnp3_status_t st;
  dnp_lab_t *l = solar_os_dnp3_lock(app.service, &st);
  if (load && st.running) {
    solar_os_dnp3_unlock(app.service);
    snprintf(app.notice, sizeof(app.notice), "Stop before loading");
    return false;
  }
  dnp_lab_t *copy = solar_os_memory_alloc(
      sizeof(*copy), SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "dnp3.profile");
  if (copy) {
    dnp_lab_init(copy, &l->config);
    memcpy(copy->points, l->points, sizeof(copy->points));
  }
  solar_os_dnp3_unlock(app.service);
  if (!copy)
    return false;
  FILE *f = *path ? fopen(path, load ? "r" : "w") : NULL;
  bool ok = f != NULL;
  if (f && load) {
    char line[224];
    unsigned row = 0;
    while (fgets(line, sizeof(line), f)) {
      row++;
      if (line[0] == '#' || line[0] == '\n')
        continue;
      if (!strchr(line, '\n') && !feof(f)) {
        ok = false;
        break;
      }
      if (!strncmp(line, "poll", 4) || !strncmp(line, "control", 7) ||
          !dnp_lab_command(copy, line, false, app.notice, sizeof(app.notice))) {
        ok = false;
        break;
      }
    }
    if (ferror(f))
      ok = false;
    if (!ok)
      snprintf(app.notice, sizeof(app.notice),
               "Invalid profile at line %u; settings retained", row);
    if (ok) {
      l = solar_os_dnp3_lock(app.service, NULL);
      l->config = copy->config;
      memcpy(l->points, copy->points, sizeof(l->points));
      solar_os_dnp3_unlock(app.service);
    }
  } else if (f) {
    dnp_lab_config_t *c = &copy->config;
    char line[200];
    fprintf(f, "# SolarOS DNP3 profile; never starts transport\nmode %s\n",
            modes[c->mode]);
    for (unsigned i = 0; i < 2; i++) {
      endpoint_text(line, sizeof(line), 'a' + i, &c->endpoint[i]);
      fprintf(f, "%s\n", line);
    }
    fprintf(f,
            "addresses %u %u\ninterval %lu\ntimeout %lu\npoints %u\nrule "
            "clear\nfault clear\n",
            c->master, c->outstation, (unsigned long)c->interval_ms,
            (unsigned long)c->timeout_ms, c->points);
    for (unsigned i = 0; i < DNP_LAB_POINTS; i++) {
      dnp_lab_point_t *p = &copy->points[i];
      fprintf(f,
              "point bi %u %u %u\npoint bo %u %u %u\npoint ai %u %ld %u\npoint "
              "ao %u %ld %u\n",
              i, p->binary, p->flags, i, p->binary_output, p->output_flags, i,
              (long)p->analog, p->analog_flags, i, (long)p->analog_output,
              p->analog_output_flags);
    }
    for (unsigned i = 0; i < DNP_LAB_RULES; i++) {
      dnp_lab_rule_t *r = &c->rules[i];
      if (!r->enabled)
        continue;
      const char *dir = r->direction ? "b2a" : "a2b";
      if (r->kind <= DNP_LAB_LINK_DST)
        fprintf(f, "rule add link %s %s %u %u\n", dir,
                r->kind == DNP_LAB_LINK_SRC ? "src" : "dst", r->index,
                r->replacement);
      else if (r->kind == DNP_LAB_CONTROL_INDEX)
        fprintf(f, "rule add control %s %u %u\n", dir, r->index,
                r->replacement);
      else {
        fprintf(f, "rule add %s %s %u %.17g",
                r->kind == DNP_LAB_BINARY ? "binary" : "analog", dir, r->index,
                r->value);
        if (r->flags >= 0)
          fprintf(f, " %d", r->flags);
        fputc('\n', f);
      }
    }
    for (unsigned i = 0; i < 2; i++) {
      dnp_lab_fault_t *v = &c->faults[i];
      const char *dir = i ? "b2a" : "a2b";
      fprintf(f,
              "fault %s drop %lu\nfault %s duplicate %lu\nfault %s crc "
              "%lu\nfault %s delay %lu\nfault %s truncate %u %lu\n",
              dir, (unsigned long)v->drop_every, dir,
              (unsigned long)v->duplicate_every, dir,
              (unsigned long)v->crc_every, dir, (unsigned long)v->delay_ms, dir,
              v->truncate_bytes, (unsigned long)v->truncate_every);
      if (v->flip_mask)
        fprintf(f, "fault %s flip %u %u %lu\n", dir, v->flip_offset,
                v->flip_mask, (unsigned long)v->flip_every);
    }
    ok = !ferror(f);
  }
  if (f && fclose(f))
    ok = false;
  solar_os_memory_free(copy);
  if (ok)
    snprintf(app.notice, sizeof(app.notice), "Profile %s",
             load ? "loaded" : "saved");
  else if (!f)
    snprintf(app.notice, sizeof(app.notice), "Cannot open profile path");
  return ok;
}
static void command(const char *s) {
  if (!strncmp(s, "save ", 5)) {
    profile(s + 5, false);
    return;
  }
  if (!strncmp(s, "load ", 5)) {
    profile(s + 5, true);
    return;
  }
  if (!strncmp(s, "log ", 4)) {
    for (unsigned i = 0; i < 5; i++)
      log_pending();
    if (app.log) {
      fprintf(app.log, "# log_lost=%" PRIu64 "\n", app.log_lost);
      fclose(app.log);
      app.log = NULL;
    }
    if (strcmp(s + 4, "stop")) {
      app.log = fopen(s + 4, "wx");
      dnp_lab_t *l = solar_os_dnp3_lock(app.service, NULL);
      app.log_next = l->events + 1;
      app.log_lost = 0;
      solar_os_dnp3_unlock(app.service);
    }
    snprintf(app.notice, sizeof(app.notice), "%s",
             app.log ? "Logging received/edited/transmitted records"
             : !strcmp(s + 4, "stop") ? "Log stopped"
                                      : "Cannot create new log path");
    return;
  }
  solar_os_dnp3_command(app.service, s, app.notice, sizeof(app.notice));
}
static void render(void) {
  if (app.suspended || !app.service)
    return;
  unsigned rows = solar_os_tui_rows(&app.tui),
           cols = solar_os_tui_cols(&app.tui);
  if (rows < 6)
    return;
  unsigned count = rows - 5;
  if (count > 40)
    count = 40;
  memset(app.lines, 0, sizeof(app.lines));
  solar_os_dnp3_status_t st;
  dnp_lab_t *l = solar_os_dnp3_lock(app.service, &st);
  char title[160];
  snprintf(title, sizeof(title), "DNP3 %s %s A:%s B:%s", modes[l->config.mode],
           st.running ? "RUN" : "STOP", st.connected[0] ? "up" : "-",
           st.connected[1] ? "up" : "-");
  if (app.tab == 0) {
    if (!app.frozen)
      app.view_end = l->events;
    uint64_t oldest =
        l->events > DNP_LAB_HISTORY ? l->events - DNP_LAB_HISTORY + 1 : 1;
    uint64_t end = app.view_end > app.scroll ? app.view_end - app.scroll : 0;
    if (end > l->events)
      end = l->events;
    uint64_t first = end >= count ? end - count + 1 : 1;
    if (first < oldest)
      first = oldest;
    for (unsigned i = 0; i < count && first + i <= end; i++) {
      dnp_lab_event_t *e = &l->history[(first + i - 1) % DNP_LAB_HISTORY];
      snprintf(app.lines[i], 200, "%lu %s %-6s %s", (unsigned long)e->at,
               e->direction ? "B>A" : "A>B", e->stage, e->text);
      if (app.hex && e->length) {
        size_t at = strlen(app.lines[i]);
        if (at > 50)
          at = 50;
        at += snprintf(app.lines[i] + at, 200 - at, " [%u] ", e->length);
        for (unsigned j = 0; j < e->length && at + 3 < 200; j++)
          at += snprintf(app.lines[i] + at, 200 - at, "%02x", e->data[j]);
      }
    }
    if (app.hex && end >= oldest) {
      uint64_t seq = end;
      while (seq >= oldest && !l->history[(seq - 1) % DNP_LAB_HISTORY].length)
        seq--;
      if (seq >= oldest) {
        dnp_lab_event_t *e = &l->history[(seq - 1) % DNP_LAB_HISTORY];
        memset(app.lines, 0, sizeof(app.lines));
        snprintf(app.lines[0], 200, "%lu %s %s %u bytes", (unsigned long)e->at,
                 e->direction ? "B>A" : "A>B", e->stage, e->length);
        snprintf(app.lines[1], 200, "%s", e->text);
        for (unsigned i = 0; i + 2 < count && i * 16 < e->length; i++) {
          size_t at = snprintf(app.lines[i + 2], 200, "%03x: ", i * 16);
          for (unsigned j = i * 16; j < (i + 1) * 16 && j < e->length; j++)
            at +=
                snprintf(app.lines[i + 2] + at, 200 - at, "%02x ", e->data[j]);
        }
      }
    }
  } else if (app.tab == 1) {
    char settings[12][200];
    memset(settings, 0, sizeof(settings));
    snprintf(settings[0], 200, "mode %s", modes[l->config.mode]);
    endpoint_text(settings[1], 200, 'a', &l->config.endpoint[0]);
    endpoint_text(settings[2], 200, 'b', &l->config.endpoint[1]);
    snprintf(settings[3], 200, "addresses %u %u", l->config.master,
             l->config.outstation);
    snprintf(settings[4], 200, "interval %lu",
             (unsigned long)l->config.interval_ms);
    snprintf(settings[5], 200, "timeout %lu",
             (unsigned long)l->config.timeout_ms);
    snprintf(settings[6], 200, "points %u", l->config.points);
    snprintf(settings[7], 200, "%s", st.detail);
    for (unsigned i = 0; i < 2; i++)
      snprintf(settings[8 + i], 200,
               "%s rx:%lu tx:%lu edit:%lu drop:%lu bad:%lu skip:%lu",
               i ? "B>A" : "A>B", (unsigned long)l->stats[i].rx,
               (unsigned long)l->stats[i].tx,
               (unsigned long)l->stats[i].changed,
               (unsigned long)l->stats[i].dropped,
               (unsigned long)l->stats[i].corrupted,
               (unsigned long)l->stats[i].skipped);
    snprintf(settings[10], 200,
             "responses:%lu controls:%lu timeout:%lu errors:%lu stack:%lu",
             (unsigned long)l->responses, (unsigned long)l->controls,
             (unsigned long)l->timeouts, (unsigned long)l->errors,
             (unsigned long)st.worker_stack_free);
    for (unsigned i = 0; i < count && app.scroll + i < 11; i++)
      strcpy(app.lines[i], settings[app.scroll + i]);
  } else if (app.tab == 2) {
    for (unsigned i = 0; i < count && app.scroll + i < l->config.points; i++) {
      unsigned index = app.scroll + i;
      dnp_lab_point_t *p = l->config.mode == DNP_LAB_MASTER
                               ? &l->master_points[index]
                               : &l->points[index];
      snprintf(app.lines[i], 200,
               "%02u BI:%u AI:%ld BO:%u AO:%ld flags:%02x/%02x/%02x/%02x",
               index, p->binary, (long)p->analog, p->binary_output,
               (long)p->analog_output, p->flags, p->analog_flags,
               p->output_flags, p->analog_output_flags);
    }
  } else if (app.tab == 3) {
    unsigned line = 0;
    for (unsigned r = 0; r < DNP_LAB_RULES; r++) {
      dnp_lab_rule_t *v = &l->config.rules[r];
      if (!v->enabled)
        continue;
      if (line >= app.scroll && line - app.scroll < count)
        snprintf(app.lines[line - app.scroll], 200,
                 "%u %s %s %u -> %.7g hit:%lu skip:%lu", r,
                 v->direction ? "B>A" : "A>B",
                 v->kind == 0   ? "src"
                 : v->kind == 1 ? "dst"
                 : v->kind == 2 ? "control"
                 : v->kind == 3 ? "binary"
                                : "analog",
                 v->index, v->kind <= 2 ? (double)v->replacement : v->value,
                 (unsigned long)v->hits, (unsigned long)v->skipped);
      line++;
    }
    for (unsigned i = 0; i < 2; i++) {
      dnp_lab_fault_t *f = &l->config.faults[i];
      if (line >= app.scroll && line - app.scroll < count)
        snprintf(app.lines[line - app.scroll], 200,
                 "%s drop:%lu dup:%lu crc:%lu delay:%lu trunc:%u/%lu "
                 "flip:%u/%02x/%lu",
                 i ? "B>A" : "A>B", (unsigned long)f->drop_every,
                 (unsigned long)f->duplicate_every, (unsigned long)f->crc_every,
                 (unsigned long)f->delay_ms, f->truncate_bytes,
                 (unsigned long)f->truncate_every, f->flip_offset, f->flip_mask,
                 (unsigned long)f->flip_every);
      line++;
    }
  } else
    for (unsigned i = 0;
         i < count && app.scroll + i < sizeof(help) / sizeof(help[0]); i++)
      snprintf(app.lines[i], 200, "%s", help[app.scroll + i]);
  solar_os_dnp3_unlock(app.service);
  solar_os_tui_clear(&app.tui);
  solar_os_tui_write_cell(&app.tui, 0, 0, cols, title, SOLAR_OS_TUI_ATTR_BOLD);
  static const char *tabs[] = {"Traffic", "Settings", "Points", "Rules/faults",
                               "Help"};
  char tabline[100];
  snprintf(tabline, sizeof(tabline), "[%s] Tab next %s", tabs[app.tab],
           app.frozen ? "FROZEN" : "live");
  solar_os_tui_write_cell(&app.tui, 1, 0, cols, tabline, 0);
  for (unsigned i = 0; i < count; i++)
    solar_os_tui_write_cell(&app.tui, i + 2, 0, cols, app.lines[i], 0);
  solar_os_tui_write_cell(&app.tui, rows - 3, 0, cols, app.notice, 0);
  char input[240];
  snprintf(input, sizeof(input), ":%s", app.command);
  const char *shown = app.input ? input : "s start/stop :command ?help";
  if (app.input && strlen(shown) > cols)
    shown += strlen(shown) - cols;
  solar_os_tui_write_cell(&app.tui, rows - 2, 0, cols, shown, 0);
  solar_os_tui_write_cell(&app.tui, rows - 1, 0, cols,
                          "Tab view Up/Down scroll q exit", 0);
  solar_os_tui_refresh(&app.tui);
}
static esp_err_t start(solar_os_context_t *ctx) {
  esp_err_t error = solar_os_dnp3_create(&app.service);
  if (error != ESP_OK)
    return error;
  bool demo = false;
  for (int i = 1; i < solar_os_context_argc(ctx); i++) {
    const char *s = solar_os_context_argv(ctx, i);
    if (!strcmp(s, "--demo"))
      demo = true;
    else if (!strcmp(s, "--config") && i + 1 < solar_os_context_argc(ctx)) {
      if (!profile(solar_os_context_argv(ctx, ++i), true)) {
        solar_os_context_finish(ctx, 1, app.notice);
        return ESP_OK;
      }
    } else {
      solar_os_context_finish(ctx, 2, "dnp3 [--demo] [--config FILE]");
      return ESP_OK;
    }
  }
  error = solar_os_tui_screen_begin(&app.tui, ctx);
  if (error != ESP_OK)
    return error;
  solar_os_tui_enable_diff(&app.tui, true);
  snprintf(app.notice, sizeof(app.notice),
           "Stopped; configure endpoints then :start. ? for help");
  if (demo) {
    command("mode demo");
    command("start");
  }
  render();
  return ESP_OK;
}
static void suspend_app(solar_os_context_t *ctx) {
  (void)ctx;
  app.suspended = true;
}
static void resume_app(solar_os_context_t *ctx) {
  (void)ctx;
  app.suspended = false;
  solar_os_tui_attach_session(&app.tui);
  render();
}
static void stop(solar_os_context_t *ctx) {
  (void)ctx;
  if (app.service) {
    solar_os_dnp3_halt(app.service);
    for (unsigned i = 0; i < 5; i++)
      log_pending();
    if (app.log) {
      fprintf(app.log, "# log_lost=%" PRIu64 "\n", app.log_lost);
      fclose(app.log);
      app.log = NULL;
    }
    solar_os_dnp3_destroy(app.service);
    app.service = NULL;
  }
  solar_os_tui_end(&app.tui);
}
static bool event(solar_os_context_t *ctx, const solar_os_event_t *e) {
  if (e->type == SOLAR_OS_EVENT_TICK) {
    log_pending();
    render();
    return true;
  }
  if (e->type != SOLAR_OS_EVENT_CHAR || app.suspended)
    return false;
  unsigned char ch = e->data.ch;
  if (ch == SOLAR_OS_KEY_APP_EXIT) {
    solar_os_context_finish(ctx, 0, NULL);
    return true;
  }
  if (app.input) {
    size_t n = strlen(app.command);
    if (ch == '\r' || ch == '\n') {
      command(app.command);
      app.input = false;
      app.command[0] = 0;
    } else if (ch == 27) {
      app.input = false;
      app.command[0] = 0;
    } else if (ch == 8 || ch == 127) {
      if (n)
        app.command[n - 1] = 0;
    } else if (ch >= 32 && ch < 127 && n + 1 < sizeof(app.command)) {
      app.command[n] = ch;
      app.command[n + 1] = 0;
    }
  } else if (ch == 'q' || ch == 27)
    solar_os_context_finish(ctx, 0, NULL);
  else if (ch == ':') {
    app.input = true;
    app.command[0] = 0;
  } else if (ch == '\t') {
    app.tab = (app.tab + 1) % 5;
    app.scroll = 0;
  } else if (ch == '?') {
    app.tab = 4;
    app.scroll = 0;
  } else if (ch == ' ') {
    app.frozen = !app.frozen;
    app.scroll = 0;
  } else if (ch == 'x')
    app.hex = !app.hex;
  else if (ch == 's') {
    solar_os_dnp3_status_t status;
    solar_os_dnp3_lock(app.service, &status);
    solar_os_dnp3_unlock(app.service);
    command(status.running ? "stop" : "start");
  } else if (ch == SOLAR_OS_KEY_UP) {
    if (app.tab == 0) {
      if (app.scroll < 127)
        app.scroll++;
      app.frozen = true;
    } else if (app.scroll)
      app.scroll--;
  } else if (ch == SOLAR_OS_KEY_DOWN) {
    if (app.tab == 0) {
      if (app.scroll)
        app.scroll--;
    } else if (app.scroll < 31)
      app.scroll++;
  } else if ((ch == '\r' || ch == '\n') && app.tab == 1 && app.scroll < 7) {
    dnp_lab_t *l = solar_os_dnp3_lock(app.service, NULL);
    unsigned row = app.scroll;
    if (row == 0)
      snprintf(app.command, sizeof(app.command), "mode %s",
               modes[l->config.mode]);
    else if (row == 1 || row == 2)
      endpoint_text(app.command, sizeof(app.command), 'a' + row - 1,
                    &l->config.endpoint[row - 1]);
    else if (row == 3)
      snprintf(app.command, sizeof(app.command), "addresses %u %u",
               l->config.master, l->config.outstation);
    else if (row == 4)
      snprintf(app.command, sizeof(app.command), "interval %lu",
               (unsigned long)l->config.interval_ms);
    else if (row == 5)
      snprintf(app.command, sizeof(app.command), "timeout %lu",
               (unsigned long)l->config.timeout_ms);
    else
      snprintf(app.command, sizeof(app.command), "points %u", l->config.points);
    solar_os_dnp3_unlock(app.service);
    app.input = true;
  }
  /* Paint on the UI tick, never once per queued input byte. */
  return true;
}
const solar_os_app_t solar_os_dnp3_app = {
    .name = "dnp3",
    .summary = "DNP3 traffic bridge and simulator",
    .app_class = SOLAR_OS_APP_CLASS_TUI,
    .flags = SOLAR_OS_APP_FLAG_RESUMABLE | SOLAR_OS_APP_FLAG_BACKGROUND_TICKS,
    .start = start,
    .suspend = suspend_app,
    .resume = resume_app,
    .stop = stop,
    .event = event,
    .state_slot = &state,
    .state_size = sizeof(app_t),
    .state_storage = SOLAR_OS_APP_STATE_EXTERNAL_REQUIRED,
    .tick_interval_ms = 200};
