#include "solar_os_organizer_io.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static void put(uint8_t *p, uint32_t n) {
  for (unsigned i = 0; i < 4; i++)
    p[i] = n >> (8 * i);
}
static uint32_t get(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static uint32_t crc_update(uint32_t crc, const void *data, size_t n) {
  const uint8_t *p = data;
  while (n--) {
    crc ^= *p++;
    for (unsigned i = 0; i < 8; i++)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return crc;
}
static uint32_t payload_crc(const uint8_t meta[56], const pim_db_t *d) {
  uint32_t crc = crc_update(~0U, meta, 56);
  crc = crc_update(crc, d->event, d->events * sizeof(d->event[0]));
  crc = crc_update(crc, d->person, d->contacts * sizeof(d->person[0]));
  crc = crc_update(crc, d->task, d->tasks * sizeof(d->task[0]));
  return ~crc;
}
static uint32_t payload_size(const pim_db_t *d) {
  return 56 + d->events * sizeof(d->event[0]) +
         d->contacts * sizeof(d->person[0]) + d->tasks * sizeof(d->task[0]);
}
bool pim_write(FILE *f, const pim_db_t *d) {
  if (!pim_valid(d))
    return false;
  uint8_t h[16] = {0}, meta[56] = {0};
  memcpy(h, "SOLPIM02", 8);
  put(meta, d->next_id);
  put(meta + 4, d->generation);
  put(meta + 8, d->events);
  put(meta + 12, d->contacts);
  put(meta + 16, d->tasks);
  memcpy(meta + 20, d->namespace_id, 33);
  put(h + 8, payload_size(d));
  put(h + 12, payload_crc(meta, d));
  return fwrite(h, 1, 16, f) == 16 && fwrite(meta, 1, 56, f) == 56 &&
         fwrite(d->event, sizeof(d->event[0]), d->events, f) == d->events &&
         fwrite(d->person, sizeof(d->person[0]), d->contacts, f) ==
             d->contacts &&
         fwrite(d->task, sizeof(d->task[0]), d->tasks, f) == d->tasks &&
         !ferror(f);
}
bool pim_read(FILE *f, pim_db_t *d, char *error, size_t n) {
  uint8_t h[16], meta[56];
  if (fread(h, 1, 16, f) != 16 || memcmp(h, "SOLPIM02", 8) ||
      fread(meta, 1, 56, f) != 56 || get(meta + 8) > PIM_EVENTS ||
      get(meta + 12) > PIM_CONTACTS || get(meta + 16) > PIM_TASKS ||
      !memchr(meta + 20, 0, 33))
    goto invalid;
  pim_init(d, (char *)meta + 20);
  d->next_id = get(meta);
  d->generation = get(meta + 4);
  d->events = get(meta + 8);
  d->contacts = get(meta + 12);
  d->tasks = get(meta + 16);
  if (get(h + 8) != payload_size(d) ||
      fread(d->event, sizeof(d->event[0]), d->events, f) != d->events ||
      fread(d->person, sizeof(d->person[0]), d->contacts, f) != d->contacts ||
      fread(d->task, sizeof(d->task[0]), d->tasks, f) != d->tasks ||
      fgetc(f) != EOF || ferror(f) || get(h + 12) != payload_crc(meta, d) ||
      !pim_valid(d))
    goto invalid;
  return true;
invalid:
  snprintf(error, n, "Invalid/corrupt organizer backup; original retained");
  return false;
}
/* Fold generated content lines at 72 bytes without breaking UTF-8. */
static bool line(FILE *f, const char *key, const char *text) {
  char out[2048];
  size_t at = 0;
  int k = snprintf(out, sizeof(out), "%s:", key);
  if (k < 0 || (size_t)k >= sizeof(out))
    return false;
  at = k;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    const char *esc = *p == '\\'   ? "\\\\"
                      : *p == '\n' ? "\\n"
                      : *p == ';'  ? "\\;"
                      : *p == ','  ? "\\,"
                                   : NULL;
    if (*p == '\r')
      continue;
    if (at + (esc ? 2 : 1) >= sizeof(out))
      return false;
    if (esc) {
      out[at++] = esc[0];
      out[at++] = esc[1];
    } else
      out[at++] = *p;
  }
  size_t pos = 0;
  while (pos < at) {
    size_t part = at - pos;
    if (part > 72) {
      part = 72;
      while (part && (out[pos + part] & 0xc0) == 0x80)
        part--;
    }
    if (pos && fputc(' ', f) == EOF)
      return false;
    if (fwrite(out + pos, 1, part, f) != part || fputs("\r\n", f) == EOF)
      return false;
    pos += part;
  }
  return !ferror(f);
}
static void date_text(int32_t day, int minute, char out[32]) {
  int y;
  unsigned m, d;
  pim_civil(day, &y, &m, &d);
  if (minute < 0)
    snprintf(out, 32, "%04d%02u%02u", y, m, d);
  else
    snprintf(out, 32, "%04d%02u%02uT%02u%02u00", y, m, d, minute / 60,
             minute % 60);
}
static void rule_text(unsigned r, int32_t origin, char *out, size_t n) {
  int y;
  unsigned month, day;
  pim_civil(origin, &y, &month, &day);
  if (r == PIM_MONTHLY && day > 28) {
    size_t at = snprintf(out, n, "FREQ=MONTHLY;BYMONTHDAY=");
    for (unsigned d = 28; d <= day; d++)
      at += snprintf(out + at, n - at, "%s%u", d == 28 ? "" : ",", d);
    snprintf(out + at, n - at, ";BYSETPOS=-1");
  } else if (r == PIM_YEARLY && month == 2 && day == 29)
    snprintf(out, n, "FREQ=YEARLY;BYMONTH=2;BYMONTHDAY=-1");
  else
    snprintf(out, n, "%s",
             r == PIM_DAILY                              ? "FREQ=DAILY"
             : r == PIM_WEEKLY                           ? "FREQ=WEEKLY"
             : r == PIM_MONTHLY || r == PIM_MONTHLY_SKIP ? "FREQ=MONTHLY"
                                                         : "FREQ=YEARLY");
}
static bool repeat(FILE *f, unsigned r, int32_t origin) {
  if (!r)
    return true;
  char rule[128];
  rule_text(r, origin, rule, sizeof(rule));
  return fprintf(f, "RRULE:%s\r\n", rule) > 0;
}

bool pim_export(FILE *f, const pim_db_t *db, unsigned kind) {
  char t[32], end[32];
  if (kind != 1)
    fprintf(f, "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//SolarOS//Organizer/"
               "/EN\r\n");
  if (kind == 0)
    for (unsigned i = 0; i < db->events; i++) {
      const pim_event_t *e = &db->event[i];
      fprintf(f, "BEGIN:VEVENT\r\n");
      line(f, "UID", e->uid);
      line(f, "SUMMARY", e->title);
      line(f, "LOCATION", e->location);
      line(f, "DESCRIPTION", e->notes);
      line(f, "CATEGORIES", e->category);
      date_text(e->day, e->all_day ? -1 : e->minute, t);
      int64_t finish = (int64_t)e->day * 1440 + e->minute + e->duration;
      date_text(finish / 1440, e->all_day ? -1 : finish % 1440, end);
      fprintf(f, "DTSTART%s:%s\r\nDTEND%s:%s\r\n",
              e->all_day ? ";VALUE=DATE" : "", t,
              e->all_day ? ";VALUE=DATE" : "", end);
      repeat(f, e->repeat, e->day);
      if (e->reminder >= 0)
        fprintf(f,
                "BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT%"
                "dM\r\nDESCRIPTION:Appointment\r\nEND:VALARM\r\n",
                e->reminder);
      fprintf(f, "END:VEVENT\r\n");
    }
  if (kind == 1)
    for (unsigned i = 0; i < db->contacts; i++) {
      const pim_contact_t *p = &db->person[i];
      fprintf(f, "BEGIN:VCARD\r\nVERSION:3.0\r\n");
      line(f, "UID", p->uid);
      line(f, "FN", p->name);
      line(f, "ORG", p->company);
      line(f, "TEL;TYPE=CELL", p->phone);
      line(f, "TEL;TYPE=WORK", p->work_phone);
      line(f, "EMAIL", p->email);
      line(f, "LABEL", p->address);
      line(f, "NOTE", p->notes);
      line(f, "CATEGORIES", p->category);
      if (p->birthday) {
        date_text(p->birthday, -1, t);
        fprintf(f, "BDAY:%s\r\n", t);
      }
      fprintf(f, "END:VCARD\r\n");
    }
  if (kind == 2)
    for (unsigned i = 0; i < db->tasks; i++) {
      const pim_task_t *p = &db->task[i];
      fprintf(f, "BEGIN:VTODO\r\n");
      line(f, "UID", p->uid);
      line(f, "SUMMARY", p->title);
      line(f, "DESCRIPTION", p->notes);
      line(f, "CATEGORIES", p->category);
      fprintf(f, "PRIORITY:%u\r\nSTATUS:%s\r\n",
              p->priority == 1   ? 1
              : p->priority == 2 ? 5
                                 : 9,
              p->done ? "COMPLETED" : "NEEDS-ACTION");
      if (p->due) {
        date_text(p->due, -1, t);
        fprintf(f, "DUE;VALUE=DATE:%s\r\n", t);
      }
      if (p->origin) {
        date_text(p->origin, -1, t);
        fprintf(f, "X-SOLAROS-ORIGIN:%s\r\n", t);
      }
      repeat(f, p->repeat, p->origin ? p->origin : p->due);
      fprintf(f, "END:VTODO\r\n");
    }
  if (kind != 1)
    fprintf(f, "END:VCALENDAR\r\n");
  return !ferror(f);
}
static bool copy(char *dst, size_t n, const char *value) {
  size_t at = 0;
  for (; *value; value++) {
    char c = *value;
    if (c == '\\') {
      c = *++value;
      if (!c)
        return false;
      if (c == 'n' || c == 'N')
        c = '\n';
      else if (c != '\\' && c != ';' && c != ',')
        return false;
    }
    if (at + 1 >= n)
      return false;
    dst[at++] = c;
  }
  dst[at] = 0;
  return true;
}
static bool numeric(const char *s, long lo, long hi, long *out) {
  char *end;
  if (!*s)
    return false;
  long v = strtol(s, &end, 10);
  if (*end || v < lo || v > hi)
    return false;
  *out = v;
  return true;
}
static bool dt(const char *s, int32_t *day, uint16_t *minute, bool *all) {
  size_t n = strlen(s);
  if (n != 8 && n != 15)
    return false;
  for (unsigned i = 0; i < n; i++)
    if (i != 8 && !isdigit((unsigned char)s[i]))
      return false;
  char d[11];
  snprintf(d, sizeof(d), "%.4s-%.2s-%.2s", s, s + 4, s + 6);
  if (!pim_date_parse(d, day))
    return false;
  *all = n == 8;
  *minute = 0;
  if (n == 15) {
    if (s[8] != 'T' || s[13] != '0' || s[14] != '0')
      return false;
    char t[] = {s[9], s[10], ':', s[11], s[12], 0};
    if (!pim_time_parse(t, minute))
      return false;
  }
  return true;
}
static int recur(const char *s, int32_t origin) {
  int y;
  unsigned month, day;
  pim_civil(origin, &y, &month, &day);
  if (!strcmp(s, "FREQ=DAILY"))
    return PIM_DAILY;
  if (!strcmp(s, "FREQ=WEEKLY"))
    return PIM_WEEKLY;
  if (!strcmp(s, "FREQ=MONTHLY"))
    return day > 28 ? PIM_MONTHLY_SKIP : PIM_MONTHLY;
  if (!strcmp(s, "FREQ=YEARLY"))
    return month == 2 && day == 29 ? PIM_YEARLY_SKIP : PIM_YEARLY;
  char expected[128];
  rule_text(PIM_MONTHLY, origin, expected, sizeof(expected));
  if (!strcmp(s, expected))
    return PIM_MONTHLY;
  rule_text(PIM_YEARLY, origin, expected, sizeof(expected));
  if (!strcmp(s, expected))
    return PIM_YEARLY;
  return -1;
}
typedef struct {
  unsigned kind, depth;
  char rule[128];
  bool active, alarm, saw_start, saw_end;
  int32_t end_day;
  uint16_t end_minute;
  pim_event_t event;
  pim_contact_t person;
  pim_task_t task;
} parser_t;
static bool property(parser_t *p, char *s) {
  char *value = strchr(s, ':');
  if (!value)
    return false;
  *value++ = 0;
  char *params = strchr(s, ';');
  if (params)
    *params++ = 0;
  if (!strcasecmp(s, "BEGIN")) {
    if (!strcasecmp(value, "VALARM") && p->kind == 0 && !p->alarm) {
      p->alarm = true;
      return true;
    }
    return false;
  }
  if (p->alarm) {
    if (!strcasecmp(s, "END") && !strcasecmp(value, "VALARM")) {
      p->alarm = false;
      return true;
    }
    if (!strcasecmp(s, "TRIGGER")) {
      long v;
      char number[16];
      size_t len = strlen(value);
      if (len < 5 || strncmp(value, "-PT", 3) || value[len - 1] != 'M' ||
          len - 4 >= sizeof(number))
        return false;
      memcpy(number, value + 3, len - 4);
      number[len - 4] = 0;
      if (!numeric(number, 0, 10080, &v))
        return false;
      p->event.reminder = v;
    }
    return true;
  }
  if (!strcasecmp(s, "UID"))
    return copy(p->kind == 0   ? p->event.uid
                : p->kind == 1 ? p->person.uid
                               : p->task.uid,
                PIM_UID, value);
  if (!strcasecmp(s, "CATEGORIES"))
    return copy(p->kind == 0   ? p->event.category
                : p->kind == 1 ? p->person.category
                               : p->task.category,
                33, value);
  if (p->kind == 1) {
    if (!strcasecmp(s, "FN"))
      return copy(p->person.name, sizeof(p->person.name), value);
    if (!strcasecmp(s, "ORG"))
      return copy(p->person.company, sizeof(p->person.company), value);
    if (!strcasecmp(s, "TEL")) {
      char *dest = params && pim_contains(params, "work") ? p->person.work_phone
                   : !*p->person.phone ? p->person.phone
                                       : p->person.work_phone;
      if (*dest)
        return false;
      return copy(dest, 41, value);
    }
    if (!strcasecmp(s, "EMAIL")) {
      if (*p->person.email)
        return false;
      return copy(p->person.email, sizeof(p->person.email), value);
    }
    if (!strcasecmp(s, "LABEL"))
      return copy(p->person.address, sizeof(p->person.address), value);
    if (!strcasecmp(s, "ADR")) {
      if (*p->person.address)
        return true;
      char address[161];
      if (!copy(address, sizeof(address), value))
        return false;
      for (char *a = address; *a; a++)
        if (*a == ';')
          *a = ' ';
      snprintf(p->person.address, sizeof(p->person.address), "%s", address);
      return true;
    }
    if (!strcasecmp(s, "NOTE"))
      return copy(p->person.notes, sizeof(p->person.notes), value);
    if (!strcasecmp(s, "BDAY")) {
      uint16_t m;
      bool all;
      if (strlen(value) == 10)
        return pim_date_parse(value, &p->person.birthday);
      return dt(value, &p->person.birthday, &m, &all) && all;
    }
    return true;
  }
  if (!strcasecmp(s, "SUMMARY"))
    return copy(p->kind == 0 ? p->event.title : p->task.title, 81, value);
  if (!strcasecmp(s, "DESCRIPTION"))
    return copy(p->kind == 0 ? p->event.notes : p->task.notes, 257, value);
  if (!strcasecmp(s, "RRULE")) {
    if (strlen(value) >= sizeof(p->rule))
      return false;
    strcpy(p->rule, value);
    return true;
  }
  if (p->kind == 0) {
    if (!strcasecmp(s, "LOCATION"))
      return copy(p->event.location, sizeof(p->event.location), value);
    if (!strcasecmp(s, "DTSTART") || !strcasecmp(s, "DTEND")) {
      if (params && strcasecmp(params, "VALUE=DATE"))
        return false;
      bool all;
      int32_t day;
      uint16_t minute;
      if (!dt(value, &day, &minute, &all))
        return false;
      if (!strcasecmp(s, "DTSTART")) {
        p->event.day = day;
        p->event.minute = minute;
        p->event.all_day = all;
        p->saw_start = true;
      } else {
        p->end_day = day;
        p->end_minute = minute;
        p->saw_end = true;
      }
      return true;
    }
    if (!strcasecmp(s, "DURATION"))
      return false;
  } else {
    if (!strcasecmp(s, "DUE") || !strcasecmp(s, "X-SOLAROS-ORIGIN")) {
      int32_t d;
      uint16_t m;
      bool all;
      if (params && strcasecmp(params, "VALUE=DATE"))
        return false;
      if (!dt(value, &d, &m, &all) || !all)
        return false;
      if (!strcasecmp(s, "DUE"))
        p->task.due = d;
      else
        p->task.origin = d;
      return true;
    }
    if (!strcasecmp(s, "PRIORITY")) {
      long v;
      if (!numeric(value, 0, 9, &v))
        return false;
      p->task.priority = v && v <= 4 ? 1 : v >= 7 ? 3 : 2;
      return true;
    }
    if (!strcasecmp(s, "STATUS")) {
      if (!strcasecmp(value, "COMPLETED"))
        p->task.done = 1;
      else if (!strcasecmp(value, "NEEDS-ACTION") ||
               !strcasecmp(value, "IN-PROCESS"))
        p->task.done = 0;
      else
        return false;
      return true;
    }
  }
  if (!strcasecmp(s, "EXDATE") || !strcasecmp(s, "RDATE") ||
      !strcasecmp(s, "RECURRENCE-ID"))
    return false;
  return true;
}
static bool finish(parser_t *p, pim_db_t *db) {
  if (p->alarm)
    return false;
  uint32_t id = 0;
  char *uid = p->kind == 0   ? p->event.uid
              : p->kind == 1 ? p->person.uid
                             : p->task.uid;
  unsigned count = p->kind == 0   ? db->events
                   : p->kind == 1 ? db->contacts
                                  : db->tasks,
           index = count,
           capacity = p->kind == 0   ? PIM_EVENTS
                      : p->kind == 1 ? PIM_CONTACTS
                                     : PIM_TASKS;
  for (unsigned i = 0; i < count; i++) {
    const char *old = p->kind == 0   ? db->event[i].uid
                      : p->kind == 1 ? db->person[i].uid
                                     : db->task[i].uid;
    if (*uid && !strcmp(old, uid)) {
      index = i;
      id = p->kind == 0   ? db->event[i].id
           : p->kind == 1 ? db->person[i].id
                          : db->task[i].id;
      break;
    }
  }
  if (*p->rule) {
    int32_t origin = p->kind == 0     ? p->event.day
                     : p->task.origin ? p->task.origin
                                      : p->task.due;
    if (!origin)
      return false;
    int r = recur(p->rule, origin);
    if (r < 0)
      return false;
    if (p->kind == 0)
      p->event.repeat = r;
    else
      p->task.repeat = r;
  }
  if (index == count && count == capacity)
    return false;
  if (!id) {
    char generated[PIM_UID];
    id = pim_new_id(db, generated);
    if (!id)
      return false;
    if (!*uid)
      strcpy(uid, generated);
  }
  if (p->kind == 0) {
    if (!p->saw_start)
      return false;
    if (p->saw_end) {
      int64_t span = (int64_t)(p->end_day - p->event.day) * 1440 +
                     p->end_minute - p->event.minute;
      if (span < 1 || span > 1440)
        return false;
      p->event.duration = span;
    } else
      p->event.duration = p->event.all_day ? 1440 : 60;
    p->event.id = id;
    if (index < count) {
      const pim_event_t *old = &db->event[index];
      p->event.contact = old->contact;
      if (p->event.day == old->day && p->event.minute == old->minute &&
          p->event.repeat == old->repeat && p->event.reminder == old->reminder)
        p->event.notified = old->notified;
    }
    db->event[index] = p->event;
    if (index == count)
      db->events++;
  } else if (p->kind == 1) {
    p->person.id = id;
    db->person[index] = p->person;
    if (index == count)
      db->contacts++;
  } else {
    p->task.id = id;
    if (!p->task.origin)
      p->task.origin = p->task.due;
    if (index < count) {
      p->task.contact = db->task[index].contact;
      p->task.completed = db->task[index].completed;
    }
    db->task[index] = p->task;
    if (index == count)
      db->tasks++;
  }
  return true;
}
bool pim_import(FILE *f, pim_db_t *db, unsigned kind, unsigned *count,
                char *error, size_t n) {
  char physical[1024], logical[2048] = {0};
  parser_t p;
  memset(&p, 0, sizeof(p));
  p.kind = kind;
  unsigned imported = 0, row = 0;
  size_t total_bytes = 0;
  bool ok = true, container = false, closed = false;
  const char *record = kind == 0 ? "VEVENT" : kind == 1 ? "VCARD" : "VTODO";
  while (ok) {
    bool eof = fgets(physical, sizeof(physical), f) == NULL;
    if (!eof) {
      row++;
      total_bytes += strlen(physical);
      if (total_bytes > 2U * 1024U * 1024U) {
        ok = false;
        break;
      }
      if (!strchr(physical, '\n') && !feof(f)) {
        ok = false;
        break;
      }
      physical[strcspn(physical, "\r\n")] = 0;
      if (physical[0] == ' ' || physical[0] == '\t') {
        if (!*logical ||
            strlen(logical) + strlen(physical) >= sizeof(logical)) {
          ok = false;
          break;
        }
        strcat(logical, physical + 1);
        continue;
      }
    }
    if (*logical) {
      if (!strncasecmp(logical, "BEGIN:", 6) &&
          !strcasecmp(logical + 6, record)) {
        if (p.active || closed) {
          ok = false;
          break;
        }
        memset(&p, 0, sizeof(p));
        p.kind = kind;
        p.active = true;
        p.event.reminder = -1;
        p.task.priority = 2;
      } else if (!strncasecmp(logical, "END:", 4) &&
                 !strcasecmp(logical + 4, record)) {
        if (!p.active || !finish(&p, db)) {
          ok = false;
          break;
        }
        p.active = false;
        imported++;
      } else if (p.active) {
        if (!property(&p, logical)) {
          ok = false;
          break;
        }
      } else if (!strcasecmp(logical, "BEGIN:VCALENDAR")) {
        if (container || closed) {
          ok = false;
          break;
        }
        container = true;
      } else if (!strcasecmp(logical, "END:VCALENDAR")) {
        if (!container) {
          ok = false;
          break;
        }
        closed = true;
      } else if (!strncasecmp(logical, "BEGIN:", 6) ||
                 !strncasecmp(logical, "END:", 4)) {
        ok = false;
        break;
      }
    }
    if (eof)
      break;
    if (strlen(physical) >= sizeof(logical)) {
      ok = false;
      break;
    }
    strcpy(logical, physical);
  }
  if (!pim_valid(db) || ferror(f) || p.active || (!imported) ||
      (kind != 1 && (!container || !closed)))
    ok = false;
  if (!ok) {
    snprintf(error, n,
             "Import rejected near line %u: unsupported/invalid/full; original "
             "retained",
             row);
    return false;
  }
  *count = imported;
  return true;
}
