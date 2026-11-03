#include "solar_os_organizer_apps.h"
#include "solar_os_keys.h"
#include "solar_os_memory.h"
#include "solar_os_organizer.h"
#include "solar_os_tui_widgets.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef struct {
  uint32_t id;
  int32_t day;
  uint16_t index;
  uint8_t kind;
} row_t;
typedef struct {
  solar_os_tui_t tui;
  bool suspended, detail, help, truncated, dirty;
  unsigned kind, view, cursor, top, help_top, mode, field, fields, operation;
  int32_t day;
  uint32_t edit_id, form_generation, seen_generation;
  row_t rows[1024];
  unsigned count;
  char search[97], input[256], notice[160];
  char value[11][257];
  solar_os_tui_input_state_t edit;
} organizer_ui_t;
static void *states[3];
static unsigned which(solar_os_context_t *ctx) {
  const char *name = solar_os_context_argv(ctx, 0);
  return !strcmp(name, "calendar") ? 0 : !strcmp(name, "contacts") ? 1 : 2;
}
static organizer_ui_t *ui(solar_os_context_t *ctx) {
  return states[which(ctx)];
}
static const char *const names[] = {"Calendar", "Contacts", "Tasks"};
static const char *const repeats[] = {"none", "daily", "weekly", "monthly",
                                      "yearly", "monthly-skip", "yearly-skip"};
static const char *const labels[3][11] = {
    {"Title", "Date YYYY-MM-DD", "Time HH:MM or all", "Duration minutes",
     "Repeat", "Reminder min (-1 off)", "Category", "Location", "Notes",
     "Contact ID (0 none)"},
    {"Name", "Company", "Phone/mobile", "Work phone", "Email", "Postal address",
     "Birthday YYYY-MM-DD", "Category", "Notes"},
    {"Title", "Due YYYY-MM-DD (blank)", "Priority 1=high 3=low", "Repeat",
     "Category", "Notes", "Contact ID (0 none)"}};
static const unsigned limits[3][11] = {{81, 11, 6, 6, 17, 6, 33, 81, 257, 11},
                                       {81, 81, 41, 41, 97, 161, 11, 33, 257},
                                       {81, 11, 2, 17, 33, 257, 11}};
static const char *const help_lines[] = {
    "a add / e edit / d delete (confirm y); Enter details; q close",
    "Calendar: Tab month/week/agenda; arrows date or list; t today",
    "Calendar: [ / ] previous/next month; j/k select appointment",
    "Contacts: name/company/phone/work/email/address/birthday/notes",
    "Tasks: Space complete; Tab open/done/all; priority then due",
    "Repeats: none, daily, weekly, monthly, yearly",
    "Monthly/yearly repeats clamp short months, retaining original date",
    "Recurring task completion advances its due date (keeps anchor)",
    "Forms: Enter/Tab next; Up/Down field; Ctrl+S saves; Esc cancels",
    "Date YYYY-MM-DD; time HH:MM or all; reminder -1 disables",
    "Category is free text; / searches fields including category",
    "Contact IDs link an appointment/task to a personal contact",
    "i import .ics/.vcf; o export to NEW path (all records of this app)",
    "b back up ALL organizer data; R restore backup (confirm y)",
    "Interchange: local/floating ICS dates, simple FREQ repeats",
    "TZID/UTC, complex RRULEs and unsupported components are rejected",
    "vCard 3.0: name, company, two phones, one email, label/address",
    "Imports merge matching UIDs; invalid/full import changes nothing",
    "Reminders continue while apps are closed, while board is powered",
    "Small system page shows next appointment or reminder; K dismiss",
    "NES Start dismisses the reminder on the small dashboard",
    "Alarm uses existing audio service, may be silent when audio busy",
    "No power-on wake or account sync; use existing time/TZ settings",
    "Ctrl+Z suspends; fg resumes; backups include personal information"};
static bool match(const char *q, const char *a, const char *b, const char *c,
                  const char *d) {
  return pim_contains(a, q) || pim_contains(b, q) || pim_contains(c, q) ||
         pim_contains(d, q);
}
static void append(organizer_ui_t *u, unsigned kind, unsigned index,
                   uint32_t id, int32_t day) {
  if (u->count == 1024) {
    u->truncated = true;
    return;
  }
  u->rows[u->count++] =
      (row_t){.kind = kind, .index = index, .id = id, .day = day};
}
static void rebuild(organizer_ui_t *u, const pim_db_t *db) {
  uint32_t selected = u->cursor < u->count ? u->rows[u->cursor].id : 0;
  u->count = 0;
  u->truncated = false;
  if (u->kind == 0) {
    int32_t begin = u->day;
    unsigned days = u->view == 0 ? 1 : u->view == 1 ? 7 : 30;
    if (u->view == 1)
      begin -= pim_weekday(begin);
    if (*u->search)
      days = 366;
    for (unsigned d = 0; d < days; d++) {
      int32_t day = begin + d;
      for (unsigned i = 0; i < db->events; i++) {
        const pim_event_t *p = &db->event[i];
        if (pim_recur_day(p->day, p->repeat, day) == day &&
            match(u->search, p->title, p->category, p->location, p->notes))
          append(u, 0, i, p->id, day);
      }
      for (unsigned i = 0; i < db->contacts; i++) {
        const pim_contact_t *p = &db->person[i];
        if (!p->birthday)
          continue;
        if (pim_recur_day(p->birthday, PIM_YEARLY, day) == day &&
            match(u->search, p->name, p->category, p->company, p->notes))
          append(u, 1, i, p->id, day);
      }
    }
  } else if (u->kind == 1) {
    for (unsigned i = 0; i < db->contacts; i++) {
      const pim_contact_t *p = &db->person[i];
      if (match(u->search, p->name, p->company, p->category, p->notes) ||
          pim_contains(p->phone, u->search) ||
          pim_contains(p->work_phone, u->search) ||
          pim_contains(p->email, u->search) ||
          pim_contains(p->address, u->search))
        append(u, 1, i, p->id, 0);
    }
  } else
    for (unsigned i = 0; i < db->tasks; i++) {
      const pim_task_t *p = &db->task[i];
      if ((u->view == 0 && p->done) || (u->view == 1 && !p->done))
        continue;
      if (match(u->search, p->title, p->category, p->notes, ""))
        append(u, 2, i, p->id, p->due);
    }
  /* Stable insertion sort is bounded by the visible-list cap. */
  for (unsigned i = 1; i < u->count; i++) {
    row_t value = u->rows[i];
    unsigned j = i;
    while (j) {
      row_t *left = &u->rows[j - 1];
      int order = 0;
      if (u->kind == 0) {
        if (left->day != value.day)
          order = left->day > value.day ? 1 : -1;
        else {
          unsigned a = left->kind == 1 ? 0 : db->event[left->index].minute,
                   b = value.kind == 1 ? 0 : db->event[value.index].minute;
          order = a > b ? 1 : a < b ? -1 : 0;
        }
      } else if (u->kind == 1)
        order = strcasecmp(db->person[left->index].name,
                           db->person[value.index].name);
      else {
        const pim_task_t *a = &db->task[left->index],
                         *b = &db->task[value.index];
        if (a->done != b->done)
          order = a->done > b->done ? 1 : -1;
        else if (a->priority != b->priority)
          order = a->priority > b->priority ? 1 : -1;
        else {
          int32_t da = a->due ? a->due : INT32_MAX,
                  dbb = b->due ? b->due : INT32_MAX;
          order = da > dbb ? 1 : da < dbb ? -1 : strcmp(a->title, b->title);
        }
      }
      if (order <= 0)
        break;
      u->rows[j] = *left;
      j--;
    }
    u->rows[j] = value;
  }
  u->cursor = 0;
  for (unsigned i = 0; i < u->count; i++)
    if (u->rows[i].id == selected) {
      u->cursor = i;
      break;
    }
  if (u->top > u->cursor)
    u->top = u->cursor;
  u->seen_generation = db->generation;
}
static void cell(organizer_ui_t *u, unsigned r, const char *text,
                 unsigned attr) {
  char safe[384];
  snprintf(safe, sizeof(safe), "%s", text);
  for (char *p = safe; *p; p++)
    if ((unsigned char)*p < 32 || *p == 127)
      *p = ' ';
  if (r < solar_os_tui_rows(&u->tui))
    solar_os_tui_write_cell(&u->tui, r, 0, solar_os_tui_cols(&u->tui), safe,
                            attr);
}
static void format_fields(organizer_ui_t *u, const pim_db_t *d, bool editing) {
  memset(u->value, 0, sizeof(u->value));
  u->fields = u->kind == 0 ? 10 : u->kind == 1 ? 9 : 7;
  u->field = 0;
  u->edit_id = 0;
  u->form_generation = d->generation;
  u->edit = (solar_os_tui_input_state_t){0};
  if (u->kind == 0) {
    pim_date_text(u->day, u->value[1], 257);
    strcpy(u->value[2], "09:00");
    strcpy(u->value[3], "60");
    strcpy(u->value[4], "none");
    strcpy(u->value[5], "15");
    strcpy(u->value[9], "0");
  }
  if (u->kind == 2) {
    strcpy(u->value[2], "2");
    strcpy(u->value[3], "none");
    strcpy(u->value[6], "0");
  }
  if (editing && u->cursor < u->count) {
    row_t r = u->rows[u->cursor];
    u->edit_id = r.id;
    if (u->kind == 0) {
      const pim_event_t *p = &d->event[r.index];
      strcpy(u->value[0], p->title);
      pim_date_text(p->day, u->value[1], 257);
      snprintf(u->value[2], 257, p->all_day ? "all" : "%02u:%02u",
               p->minute / 60, p->minute % 60);
      snprintf(u->value[3], 257, "%u", p->duration);
      strcpy(u->value[4], repeats[p->repeat]);
      snprintf(u->value[5], 257, "%d", p->reminder);
      strcpy(u->value[6], p->category);
      strcpy(u->value[7], p->location);
      strcpy(u->value[8], p->notes);
      snprintf(u->value[9], 257, "%lu", (unsigned long)p->contact);
    } else if (u->kind == 1) {
      const pim_contact_t *p = &d->person[r.index];
      strcpy(u->value[0], p->name);
      strcpy(u->value[1], p->company);
      strcpy(u->value[2], p->phone);
      strcpy(u->value[3], p->work_phone);
      strcpy(u->value[4], p->email);
      strcpy(u->value[5], p->address);
      if (p->birthday)
        pim_date_text(p->birthday, u->value[6], 257);
      strcpy(u->value[7], p->category);
      strcpy(u->value[8], p->notes);
    } else {
      const pim_task_t *p = &d->task[r.index];
      strcpy(u->value[0], p->title);
      if (p->due)
        pim_date_text(p->due, u->value[1], 257);
      snprintf(u->value[2], 257, "%u", p->priority);
      strcpy(u->value[3], repeats[p->repeat]);
      strcpy(u->value[4], p->category);
      strcpy(u->value[5], p->notes);
      snprintf(u->value[6], 257, "%lu", (unsigned long)p->contact);
    }
  }
  u->mode = 1;
  u->edit.cursor = strlen(u->value[0]);
}
static bool number(const char *s, long low, long high, long *v) {
  char *end;
  if (!*s)
    return false;
  long n = strtol(s, &end, 10);
  if (*end || n < low || n > high)
    return false;
  *v = n;
  return true;
}
static int repetition(const char *s) {
  for (unsigned i = 0; i < 7; i++)
    if (!strcasecmp(s, repeats[i]))
      return i;
  return -1;
}
static bool contact_exists(const pim_db_t *d, uint32_t id) {
  if (!id)
    return true;
  for (unsigned i = 0; i < d->contacts; i++)
    if (d->person[i].id == id)
      return true;
  return false;
}
static void save_form(organizer_ui_t *u) {
  pim_db_t *copy = solar_os_organizer_copy();
  if (!copy) {
    strcpy(u->notice, "Not enough transaction memory");
    return;
  }
  uint32_t gen = copy->generation;
  bool valid = *u->value[0] != 0;
  unsigned index = u->kind == 0   ? copy->events
                   : u->kind == 1 ? copy->contacts
                                  : copy->tasks;
  unsigned capacity = u->kind == 0   ? PIM_EVENTS
                      : u->kind == 1 ? PIM_CONTACTS
                                     : PIM_TASKS;
  if (gen != u->form_generation) {
    u->form_generation = gen;
    strcpy(u->notice,
           "Store changed while editing; review then Ctrl+S to retry");
    goto done;
  }
  if (u->edit_id) {
    bool found = false;
    for (unsigned i = 0; i < index; i++) {
      uint32_t id = u->kind == 0   ? copy->event[i].id
                    : u->kind == 1 ? copy->person[i].id
                                   : copy->task[i].id;
      if (id == u->edit_id) {
        index = i;
        found = true;
        break;
      }
    }
    if (!found) {
      strcpy(u->notice, "Record was removed; cancel this edit");
      goto done;
    }
  } else if (index == capacity) {
    strcpy(u->notice, "Record capacity reached; export/archive older entries");
    goto done;
  }
  if (u->kind == 0) {
    pim_event_t *p = &copy->event[index];
    if (!u->edit_id) {
      memset(p, 0, sizeof(*p));
      p->id = pim_new_id(copy, p->uid);
      copy->events++;
    }
    int32_t day = 0;
    uint16_t minute = 0;
    bool all = !strcasecmp(u->value[2], "all");
    long duration, remind, contact;
    int repeat = repetition(u->value[4]);
    valid &= pim_date_parse(u->value[1], &day) &&
             (all || pim_time_parse(u->value[2], &minute)) &&
             number(u->value[3], 1, 1440, &duration) &&
             number(u->value[5], -1, 10080, &remind) &&
             number(u->value[9], 0, INT32_MAX, &contact) && repeat >= 0;
    if (!valid)
      goto invalid;
    if (!contact_exists(copy, contact))
      goto invalid;
    if (day != p->day || minute != p->minute || remind != p->reminder ||
        repeat != p->repeat)
      p->notified = 0;
    p->day = day;
    p->minute = all ? 0 : minute;
    p->duration = all ? 1440 : duration;
    p->all_day = all;
    p->reminder = remind;
    p->repeat = repeat;
    p->contact = contact;
    strcpy(p->title, u->value[0]);
    strcpy(p->category, u->value[6]);
    strcpy(p->location, u->value[7]);
    strcpy(p->notes, u->value[8]);
  } else if (u->kind == 1) {
    pim_contact_t *p = &copy->person[index];
    if (!u->edit_id) {
      memset(p, 0, sizeof(*p));
      p->id = pim_new_id(copy, p->uid);
      copy->contacts++;
    }
    int32_t birthday = 0;
    if (*u->value[6] && !pim_date_parse(u->value[6], &birthday))
      goto invalid;
    if (!valid)
      goto invalid;
    p->birthday = birthday;
    strcpy(p->name, u->value[0]);
    strcpy(p->company, u->value[1]);
    strcpy(p->phone, u->value[2]);
    strcpy(p->work_phone, u->value[3]);
    strcpy(p->email, u->value[4]);
    strcpy(p->address, u->value[5]);
    strcpy(p->category, u->value[7]);
    strcpy(p->notes, u->value[8]);
  } else {
    pim_task_t *p = &copy->task[index];
    if (!u->edit_id) {
      memset(p, 0, sizeof(*p));
      p->id = pim_new_id(copy, p->uid);
      copy->tasks++;
    }
    int32_t due = 0;
    long priority, contact;
    int repeat = repetition(u->value[3]);
    if (*u->value[1] && !pim_date_parse(u->value[1], &due))
      goto invalid;
    valid &= number(u->value[2], 1, 3, &priority) &&
             number(u->value[6], 0, INT32_MAX, &contact) && repeat >= 0;
    if (!valid || !contact_exists(copy, contact) || (repeat && !due))
      goto invalid;
    if (due != p->due || repeat != p->repeat)
      p->origin = due;
    p->due = due;
    p->priority = priority;
    p->repeat = repeat;
    p->contact = contact;
    strcpy(p->title, u->value[0]);
    strcpy(p->category, u->value[4]);
    strcpy(p->notes, u->value[5]);
  }
  if (solar_os_organizer_commit(copy, gen, u->notice, sizeof(u->notice))) {
    u->mode = 0;
    u->dirty = true;
  }
  goto done;
invalid:
  strcpy(
      u->notice,
      "Invalid/required field: title, date, time, repeat, range or Contact ID");
done:
  solar_os_memory_free(copy);
}
static void selected_line(const pim_db_t *d, const row_t *r, char *out,
                          size_t n) {
  char date[20];
  pim_date_text(r->day, date, sizeof(date));
  if (r->kind == 0) {
    const pim_event_t *p = &d->event[r->index];
    if (p->all_day)
      snprintf(out, n, "%s all day #%lu %s [%s]", date, (unsigned long)p->id,
               p->title, p->category);
    else
      snprintf(out, n, "%s %02u:%02u #%lu %s [%s]", date, p->minute / 60,
               p->minute % 60, (unsigned long)p->id, p->title, p->category);
  } else if (r->kind == 1) {
    const pim_contact_t *p = &d->person[r->index];
    snprintf(out, n, r->day ? "%s Birthday #%lu %s" : "%s #%lu %s",
             r->day ? date : p->company, (unsigned long)p->id, p->name);
  } else {
    const pim_task_t *p = &d->task[r->index];
    snprintf(out, n, "[%c] P%u %s #%lu %s [%s]", p->done ? 'x' : ' ',
             p->priority, date, (unsigned long)p->id, p->title, p->category);
  }
}
static void render(organizer_ui_t *u) {
  if (u->suspended)
    return;
  unsigned rows = solar_os_tui_rows(&u->tui), cols = solar_os_tui_cols(&u->tui);
  if (rows < 7)
    return;
  solar_os_tui_clear(&u->tui);
  char line[384], status[128];
  pim_db_t *d = solar_os_organizer_lock(status, sizeof(status));
  if (!d) {
    solar_os_organizer_unlock();
    cell(u, 0, status, 1);
    solar_os_tui_refresh(&u->tui);
    return;
  }
  if (d->generation != u->seen_generation || u->dirty) {
    rebuild(u, d);
    u->dirty = false;
  }
  char date[24];
  pim_date_text(u->day, date, sizeof(date));
  snprintf(line, sizeof(line), "%s %s  %u records  %s", names[u->kind],
           u->kind == 0 ? date : "", u->count, *u->search ? u->search : status);
  cell(u, 0, line, 1);
  unsigned start = 2;
  if (u->help) {
    for (unsigned i = 0;
         i < rows - 4 &&
         u->help_top + i < sizeof(help_lines) / sizeof(help_lines[0]);
         i++)
      cell(u, i + 2, help_lines[u->help_top + i], 0);
  } else if (u->mode == 1) {
    snprintf(line, sizeof(line), "%s %s  Enter next / Ctrl+S save",
             u->edit_id ? "Edit" : "New", names[u->kind]);
    cell(u, 1, line, 0);
    unsigned visible = rows - 5,
             top = u->field >= visible ? u->field - visible + 1 : 0;
    for (unsigned i = 0; i < visible && top + i < u->fields; i++) {
      unsigned field = top + i;
      snprintf(line, sizeof(line), "%s: %s", labels[u->kind][field],
               u->value[field]);
      if (field == u->field) {
        char label[96];
        snprintf(label, sizeof(label), "%s: ", labels[u->kind][field]);
        solar_os_tui_draw_input(&u->tui, i + 2, 0, cols, label, u->value[field],
                                &u->edit, SOLAR_OS_TUI_ATTR_INVERSE);
      } else
        cell(u, i + 2, line, 0);
    }
  } else if (u->detail && u->count) {
    row_t r = u->rows[u->cursor];
    selected_line(d, &r, line, sizeof(line));
    cell(u, 2, line, 1);
    if (r.kind == 0) {
      const pim_event_t *p = &d->event[r.index];
      snprintf(line, sizeof(line), "Location: %s", p->location);
      cell(u, 3, line, 0);
      snprintf(line, sizeof(line),
               "Repeat %s / %u min / reminder %d / contact #%lu",
               repeats[p->repeat], p->duration, p->reminder,
               (unsigned long)p->contact);
      cell(u, 4, line, 0);
      cell(u, 5, p->notes, 0);
    } else if (r.kind == 1) {
      const pim_contact_t *p = &d->person[r.index];
      const char *text[] = {p->company, p->phone,    p->work_phone, p->email,
                            p->address, p->category, p->notes};
      const char *labels[] = {"Company", "Phone",    "Work phone", "Email",
                              "Address", "Category", "Notes"};
      for (unsigned i = 0; i < 7; i++) {
        snprintf(line, sizeof(line), "%s: %s", labels[i], text[i]);
        cell(u, 3 + i, line, 0);
      }
      if (p->birthday) {
        pim_date_text(p->birthday, date, sizeof(date));
        cell(u, 10, date, 0);
      }
    } else {
      const pim_task_t *p = &d->task[r.index];
      cell(u, 3, p->notes, 0);
      snprintf(line, sizeof(line), "Repeat %s / completed %lu / contact #%lu",
               repeats[p->repeat], (unsigned long)p->completed,
               (unsigned long)p->contact);
      cell(u, 4, line, 0);
    }
  } else {
    if (u->kind == 0 && u->view == 0 && !*u->search) {
      int y;
      unsigned m, day;
      pim_civil(u->day, &y, &m, &day);
      snprintf(line, sizeof(line), "%04d-%02u  Tab month/week/agenda", y, m);
      cell(u, 1, line, 0);
      cell(u, 2, "Mo Tu We Th Fr Sa Su", 0);
      int32_t first = pim_day(y, m, 1);
      unsigned offset = pim_weekday(first);
      for (unsigned dd = 1; dd <= pim_month_days(y, m); dd++) {
        unsigned pos = offset + dd - 1;
        char v[4];
        bool have = false;
        for (unsigned i = 0; i < d->events; i++)
          if (pim_recur_day(d->event[i].day, d->event[i].repeat,
                            first + (int32_t)dd - 1) ==
              first + (int32_t)dd - 1) {
            have = true;
            break;
          }
        if (!have)
          for (unsigned i = 0; i < d->contacts; i++)
            if (d->person[i].birthday &&
                pim_recur_day(d->person[i].birthday, PIM_YEARLY,
                              first + (int32_t)dd - 1) ==
                    first + (int32_t)dd - 1) {
              have = true;
              break;
            }
        snprintf(v, sizeof(v), "%2u%c", dd, have ? '*' : ' ');
        solar_os_tui_write_cell(&u->tui, 3 + pos / 7, (pos % 7) * 3, 3, v,
                                dd == day ? SOLAR_OS_TUI_ATTR_INVERSE : 0);
      }
      start = 9;
    } else {
      snprintf(line, sizeof(line), "%s / Enter details / a add / ? help",
               u->kind == 0   ? (u->view == 1 ? "Week" : "Agenda")
               : u->kind == 1 ? "Personal address book"
               : u->view == 0 ? "Open"
               : u->view == 1 ? "Completed"
                              : "All tasks");
      cell(u, 1, line, 0);
    }
    unsigned visible = rows - 3 > start ? rows - 3 - start : 1;
    if (u->cursor < u->top)
      u->top = u->cursor;
    if (u->cursor >= u->top + visible)
      u->top = u->cursor - visible + 1;
    for (unsigned i = 0; i < visible && u->top + i < u->count; i++) {
      selected_line(d, &u->rows[u->top + i], line, sizeof(line));
      cell(u, start + i, line,
           u->top + i == u->cursor ? SOLAR_OS_TUI_ATTR_INVERSE : 0);
    }
    if (!u->count)
      cell(u, start, "No entries. a adds; i imports.", 0);
  }
  solar_os_organizer_unlock();
  cell(u, rows - 3,
       u->truncated ? "List capped at 1024; narrow date/search" : u->notice, 0);
  if (u->mode == 2 || u->mode == 4) {
    const char *label = u->mode == 2        ? "Search: "
                        : u->operation == 0 ? "Import path: "
                        : u->operation == 1 ? "Export NEW path: "
                        : u->operation == 2 ? "Backup NEW path: "
                                            : "Restore path: ";
    solar_os_tui_draw_input(&u->tui, rows - 2, 0, cols, label, u->input,
                            &u->edit, 0);
  } else
    cell(u, rows - 2,
         u->mode == 3   ? "Delete selected record? y / n"
         : u->mode == 5 ? "REPLACE organizer from backup? y / n"
                        : "a add e edit d delete / search Tab view",
         0);
  cell(u, rows - 1, "? help  i/o import/export  b backup  K dismiss  q quit",
       0);
  solar_os_tui_refresh(&u->tui);
}
static void mutate_selected(organizer_ui_t *u, bool remove) {
  if (!u->count)
    return;
  row_t r = u->rows[u->cursor];
  pim_db_t *d = solar_os_organizer_copy();
  if (!d)
    return;
  uint32_t gen = d->generation;
  bool ok = false;
  if (remove)
    ok = pim_remove(d, r.kind, r.id);
  else if (r.kind == 2)
    for (unsigned i = 0; i < d->tasks; i++)
      if (d->task[i].id == r.id) {
        ok = pim_task_complete(&d->task[i], solar_os_organizer_today(NULL));
        break;
      }

  if (ok)
    solar_os_organizer_commit(d, gen, u->notice, sizeof(u->notice));
  solar_os_memory_free(d);
  u->dirty = true;
  u->detail = false;
}
static esp_err_t start(solar_os_context_t *ctx) {
  organizer_ui_t *u = ui(ctx);
  u->kind = which(ctx);
  u->day = solar_os_organizer_today(NULL);
  if (!u->day)
    u->day = pim_day(2026, 1, 1);
  u->seen_generation = UINT32_MAX;
  u->dirty = true;
  esp_err_t err = solar_os_tui_screen_begin(&u->tui, ctx);
  if (err != ESP_OK)
    return err;
  solar_os_tui_enable_diff(&u->tui, true);
  solar_os_organizer_begin();
  render(u);
  return ESP_OK;
}
static void stop(solar_os_context_t *ctx) { solar_os_tui_end(&ui(ctx)->tui); }
static void suspend_ui(solar_os_context_t *ctx) { ui(ctx)->suspended = true; }
static void resume_ui(solar_os_context_t *ctx) {
  organizer_ui_t *u = ui(ctx);
  u->suspended = false;
  u->dirty = true;
  solar_os_tui_attach_session(&u->tui);
  render(u);
}
static bool event(solar_os_context_t *ctx, const solar_os_event_t *ev) {
  organizer_ui_t *u = ui(ctx);
  if (ev->type == SOLAR_OS_EVENT_TICK) {
    render(u);
    return true;
  }
  if (ev->type != SOLAR_OS_EVENT_CHAR || u->suspended)
    return false;
  unsigned char key = ev->data.ch;
  if (key == SOLAR_OS_KEY_APP_EXIT) {
    solar_os_context_finish(ctx, 0, NULL);
    return true;
  }
  if (u->help) {
    if (key == SOLAR_OS_KEY_DOWN &&
        u->help_top + 1 < sizeof(help_lines) / sizeof(help_lines[0]))
      u->help_top++;
    else if (key == SOLAR_OS_KEY_UP && u->help_top)
      u->help_top--;
    else if (key == '?' || key == 27 || key == 'q')
      u->help = false;
    return true;
  }
  if (u->mode == 1) {
    if (key == 21) {
      u->value[u->field][0] = 0;
      u->edit = (solar_os_tui_input_state_t){0};
      return true;
    }
    if (key == 27) {
      u->mode = 0;
      return true;
    }
    if (key == 19) {
      save_form(u);
      return true;
    }
    if (key == '\t' || key == '\r' || key == '\n' || key == SOLAR_OS_KEY_DOWN) {
      u->field = (u->field + 1) % u->fields;
      u->edit =
          (solar_os_tui_input_state_t){.cursor = strlen(u->value[u->field])};
      return true;
    }
    if (key == SOLAR_OS_KEY_UP) {
      u->field = u->field ? u->field - 1 : u->fields - 1;
      u->edit =
          (solar_os_tui_input_state_t){.cursor = strlen(u->value[u->field])};
      return true;
    }
    solar_os_tui_input_key(u->value[u->field], limits[u->kind][u->field],
                           &u->edit, key, solar_os_tui_cols(&u->tui));
    return true;
  }
  if (u->mode == 2 || u->mode == 4) {
    solar_os_tui_input_action_t act = solar_os_tui_input_key(
        u->input, sizeof(u->input), &u->edit, key, solar_os_tui_cols(&u->tui));
    if (act == SOLAR_OS_TUI_INPUT_CANCEL)
      u->mode = 0;
    else if (act == SOLAR_OS_TUI_INPUT_SUBMIT) {
      if (u->mode == 2) {
        snprintf(u->search, sizeof(u->search), "%.96s", u->input);
        u->cursor = u->top = 0;
        u->dirty = true;
        u->mode = 0;
      } else if (u->operation == 3)
        u->mode = 5;
      else {
        if (u->operation == 2)
          solar_os_organizer_backup(u->input, false, u->notice,
                                    sizeof(u->notice));
        else
          solar_os_organizer_transfer(u->input, u->kind, u->operation == 0,
                                      u->notice, sizeof(u->notice));
        u->mode = 0;
        u->dirty = true;
      }
    }
    return true;
  }
  if (u->mode == 3 || u->mode == 5) {
    if (key == 'y' || key == 'Y') {
      if (u->mode == 3)
        mutate_selected(u, true);
      else
        solar_os_organizer_backup(u->input, true, u->notice, sizeof(u->notice));
      u->mode = 0;
      u->dirty = true;
    } else if (key == 'n' || key == 'N' || key == 27)
      u->mode = 0;
    return true;
  }
  if (key == 'q' || key == 27) {
    if (u->detail)
      u->detail = false;
    else
      solar_os_context_finish(ctx, 0, NULL);
    return true;
  }
  if (key == '?') {
    u->help = true;
    return true;
  }
  if (key == 'K') {
    solar_os_organizer_ack();
    strcpy(u->notice, "Reminder dismissed");
    return true;
  }
  if (key == '/' || key == 'i' || key == 'o' || key == 'b' || key == 'R') {
    u->mode = key == '/' ? 2 : 4;
    u->operation = key == 'i' ? 0 : key == 'o' ? 1 : key == 'b' ? 2 : 3;
    u->input[0] = 0;
    u->edit = (solar_os_tui_input_state_t){0};
    return true;
  }
  if (key == 'a' || key == 'e') {
    pim_db_t *d = solar_os_organizer_lock(NULL, 0);
    if (d && (key == 'a' || u->count)) {
      if (key == 'e' && u->rows[u->cursor].kind != u->kind)
        strcpy(u->notice, "Edit birthdays in Contacts");
      else
        format_fields(u, d, key == 'e');
    }
    solar_os_organizer_unlock();
    return true;
  }
  if (key == 'd' && u->count) {
    if (u->rows[u->cursor].kind != u->kind)
      strcpy(u->notice, "Delete birthdays in Contacts");
    else
      u->mode = 3;
    return true;
  }
  if (key == ' ' && u->kind == 2) {
    mutate_selected(u, false);
    return true;
  }
  if (key == '\r' || key == '\n') {
    u->detail = !u->detail;
    return true;
  }
  if (key == '\t' && u->kind != 1) {
    u->view = (u->view + 1) % 3;
    u->cursor = u->top = 0;
    u->detail = false;
    u->dirty = true;
    return true;
  }
  if (u->kind == 0) {
    int32_t prior = u->day;
    if (key == 't')
      u->day = solar_os_organizer_today(NULL);
    else if (key == '[' || key == ']') {
      int y;
      unsigned m, d;
      pim_civil(u->day, &y, &m, &d);
      if (key == '[') {
        if (m == 1) {
          m = 12;
          y--;
        } else
          m--;
      } else if (++m == 13) {
        m = 1;
        y++;
      }
      if (d > pim_month_days(y, m))
        d = pim_month_days(y, m);
      int32_t next = pim_day(y, m, d);
      if (next)
        u->day = next;
    } else if (key == SOLAR_OS_KEY_LEFT)
      u->day -= u->view == 2 ? 30 : 1;
    else if (key == SOLAR_OS_KEY_RIGHT)
      u->day += u->view == 2 ? 30 : 1;
    else if (u->view == 0 && !u->detail) {
      if (key == SOLAR_OS_KEY_UP)
        u->day -= 7;
      else if (key == SOLAR_OS_KEY_DOWN)
        u->day += 7;
    }
    if (!u->day || u->day < pim_day(1900, 1, 1) ||
        u->day > pim_day(2199, 12, 31))
      u->day = prior;
    if (prior != u->day) {
      u->cursor = u->top = 0;
      u->dirty = true;
      return true;
    }
  }
  if ((key == SOLAR_OS_KEY_UP || key == 'k') && u->cursor)
    u->cursor--;
  else if ((key == SOLAR_OS_KEY_DOWN || key == 'j') && u->cursor + 1 < u->count)
    u->cursor++;
  else if (key == SOLAR_OS_KEY_HOME)
    u->cursor = 0;
  else if (key == SOLAR_OS_KEY_END && u->count)
    u->cursor = u->count - 1;
  return true;
}
#define ORGANIZER_APP(NAME, SLOT, SUMMARY)                                     \
  {.name = NAME,                                                               \
   .summary = SUMMARY,                                                         \
   .app_class = SOLAR_OS_APP_CLASS_TUI,                                        \
   .flags = SOLAR_OS_APP_FLAG_RESUMABLE,                                       \
   .start = start,                                                             \
   .stop = stop,                                                               \
   .suspend = suspend_ui,                                                      \
   .resume = resume_ui,                                                        \
   .event = event,                                                             \
   .state_slot = &states[SLOT],                                                \
   .state_size = sizeof(organizer_ui_t),                                       \
   .state_storage = SOLAR_OS_APP_STATE_EXTERNAL_REQUIRED,                      \
   .tick_interval_ms = 200}
const solar_os_app_t solar_os_calendar_app =
    ORGANIZER_APP("calendar", 0, "local appointments and reminders");
const solar_os_app_t solar_os_people_app =
    ORGANIZER_APP("contacts", 1, "personal address book");
const solar_os_app_t solar_os_tasks_app =
    ORGANIZER_APP("tasks", 2, "prioritized recurring to-do lists");
