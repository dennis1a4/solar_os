#include "solar_os_organizer_model.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
unsigned pim_month_days(int y, unsigned m) {
  static const uint8_t n[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m < 1 || m > 12 ? 0
                         : n[m - 1] + (m == 2 && y % 4 == 0 &&
                                       (y % 100 != 0 || y % 400 == 0));
}
int32_t pim_day(int y, unsigned m, unsigned d) {
  if (y < 1900 || y > 2199 || d < 1 || d > pim_month_days(y, m))
    return 0;
  y -= m <= 2;
  int era = y / 400;
  unsigned yo = y - era * 400, mp = m > 2 ? m - 3 : m + 9;
  return era * 146097 +
         (yo * 365 + yo / 4 - yo / 100 + (153 * mp + 2) / 5 + d - 1) - 719468 +
         25568;
}
void pim_civil(int32_t z, int *y, unsigned *m, unsigned *d) {
  z += 719468 - 25568;
  int era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = z - era * 146097,
           yo = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int year = yo + era * 400;
  unsigned dy = doe - (365 * yo + yo / 4 - yo / 100), mp = (5 * dy + 2) / 153;
  *d = dy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = year + (*m <= 2);
}
int pim_weekday(int32_t day) {
  int n = (day - 1) % 7;
  return n < 0 ? n + 7 : n;
}
bool pim_date_parse(const char *s, int32_t *day) {
  int y, m, d;
  char tail;
  if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-' ||
      sscanf(s, "%4d-%2d-%2d%c", &y, &m, &d, &tail) != 3)
    return false;
  for (unsigned i = 0; i < 10; i++)
    if (i != 4 && i != 7 && !isdigit((unsigned char)s[i]))
      return false;
  int32_t v = pim_day(y, m, d);
  if (!v)
    return false;
  *day = v;
  return true;
}
void pim_date_text(int32_t day, char *s, size_t n) {
  if (!day) {
    snprintf(s, n, "-");
    return;
  }
  int y;
  unsigned m, d;
  pim_civil(day, &y, &m, &d);
  snprintf(s, n, "%04d-%02u-%02u", y, m, d);
}
bool pim_time_parse(const char *s, uint16_t *out) {
  if (!s || strlen(s) != 5 || s[2] != ':' || !isdigit((unsigned char)s[0]) ||
      !isdigit((unsigned char)s[1]) || !isdigit((unsigned char)s[3]) ||
      !isdigit((unsigned char)s[4]))
    return false;
  int h = (s[0] - '0') * 10 + s[1] - '0', m = (s[3] - '0') * 10 + s[4] - '0';
  if (h > 23 || m > 59)
    return false;
  *out = h * 60 + m;
  return true;
}
bool pim_contains(const char *s, const char *q) {
  if (!*q)
    return true;
  for (; *s; s++) {
    const char *a = s, *b = q;
    while (*a && *b &&
           tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
      a++;
      b++;
    }
    if (!*b)
      return true;
  }
  return false;
}
void pim_init(pim_db_t *d, const char *ns) {
  memset(d, 0, sizeof(*d));
  d->next_id = 1;
  snprintf(d->namespace_id, sizeof(d->namespace_id), "%s", ns);
}
int32_t pim_recur_day(int32_t origin, unsigned repeat, int32_t after) {
  if (!origin || after > pim_day(2199, 12, 31))
    return 0;
  if (after <= origin)
    return origin;
  if (repeat == PIM_NONE)
    return 0;
  if (repeat == PIM_DAILY)
    return after;
  if (repeat == PIM_WEEKLY) {
    int32_t d = origin + ((after - origin + 6) / 7) * 7;
    return d > pim_day(2199, 12, 31) ? 0 : d;
  }
  int y, oy;
  unsigned m, day, om, od;
  pim_civil(after, &y, &m, &day);
  pim_civil(origin, &oy, &om, &od);
  if (repeat == PIM_MONTHLY_SKIP) {
    for (unsigned i = 0; i < 24; i++) {
      int32_t v = pim_day(y, m, od);
      if (v >= after && v >= origin)
        return v;
      if (++m == 13) {
        m = 1;
        y++;
      }
    }
    return 0;
  }
  if (repeat == PIM_YEARLY_SKIP) {
    for (unsigned i = 0; i < 9; i++, y++) {
      int32_t v = pim_day(y, om, od);
      if (v >= after && v >= origin)
        return v;
    }
    return 0;
  }
  if (repeat == PIM_MONTHLY) {
    for (unsigned i = 0; i < 2; i++) {
      unsigned md = pim_month_days(y, m), d = od > md ? md : od;
      int32_t v = pim_day(y, m, d);
      if (v >= after && v >= origin)
        return v;
      if (++m == 13) {
        m = 1;
        y++;
      }
    }
  } else if (repeat == PIM_YEARLY) {
    for (unsigned i = 0; i < 2; i++, y++) {
      unsigned md = pim_month_days(y, om), d = od > md ? md : od;
      int32_t v = pim_day(y, om, d);
      if (v >= after && v >= origin)
        return v;
    }
  }
  return 0;
}
int64_t pim_event_next(const pim_event_t *e, int64_t after) {
  int32_t day = after / 1440;
  if (after < 0 && after % 1440)
    day--;
  int32_t date = pim_recur_day(e->day, e->repeat, day);
  if (!date)
    return -1;
  int64_t value = (int64_t)date * 1440 + e->minute;
  if (value < after) {
    date = pim_recur_day(e->day, e->repeat, date + 1);
    if (!date)
      return -1;
    value = (int64_t)date * 1440 + e->minute;
  }
  return value;
}
bool pim_task_complete(pim_task_t *t, int32_t today) {
  if (t->repeat) {
    int32_t next = pim_recur_day(t->origin, t->repeat,
                                 (today > t->due ? today : t->due) + 1);
    if (!next)
      return false;
    t->due = next;
    t->done = 0;
    t->completed++;
  } else {
    t->done = !t->done;
    if (t->done)
      t->completed++;
  }
  return true;
}
uint32_t pim_new_id(pim_db_t *d, char uid[PIM_UID]) {
  if (!d->next_id || d->next_id == UINT32_MAX)
    return 0;
  uint32_t id = d->next_id++;
  snprintf(uid, PIM_UID, "%lu@%s.solaros", (unsigned long)id, d->namespace_id);
  return id;
}
static bool text(const char *s, size_t n) {
  const char *end = memchr(s, 0, n);
  if (!end)
    return false;
  for (; s < end; s++) {
    unsigned char c = *s;
    if (c == 127 || (c < 32 && c != '\n' && c != '\r' && c != '\t'))
      return false;
  }
  return true;
}
static bool date_ok(int32_t d) {
  return !d || (d >= pim_day(1900, 1, 1) && d <= pim_day(2199, 12, 31));
}
bool pim_valid(const pim_db_t *d) {
  if (d->events > PIM_EVENTS || d->contacts > PIM_CONTACTS ||
      d->tasks > PIM_TASKS || !d->next_id ||
      !text(d->namespace_id, sizeof(d->namespace_id)) || !*d->namespace_id)
    return false;
  uint32_t ids[PIM_EVENTS + PIM_CONTACTS + PIM_TASKS];
  unsigned n = 0;
  for (unsigned i = 0; i < d->events; i++) {
    const pim_event_t *p = &d->event[i];
    if (!p->day || !date_ok(p->day) || p->minute > 1439 || !p->duration ||
        p->duration > 1440 || p->reminder < -1 || p->reminder > 10080 ||
        p->repeat > PIM_YEARLY_SKIP || p->all_day > 1 ||
        (p->all_day && (p->minute || p->duration != 1440)) ||
        !text(p->uid, sizeof(p->uid)) || !*p->uid ||
        !text(p->title, sizeof(p->title)) || !*p->title ||
        !text(p->category, sizeof(p->category)) ||
        !text(p->location, sizeof(p->location)) ||
        !text(p->notes, sizeof(p->notes)))
      return false;
    ids[n++] = p->id;
  }
  for (unsigned i = 0; i < d->contacts; i++) {
    const pim_contact_t *p = &d->person[i];
    if (!date_ok(p->birthday) || !text(p->uid, sizeof(p->uid)) || !*p->uid ||
        !text(p->name, sizeof(p->name)) || !*p->name ||
        !text(p->company, sizeof(p->company)) ||
        !text(p->phone, sizeof(p->phone)) ||
        !text(p->work_phone, sizeof(p->work_phone)) ||
        !text(p->email, sizeof(p->email)) ||
        !text(p->address, sizeof(p->address)) ||
        !text(p->category, sizeof(p->category)) ||
        !text(p->notes, sizeof(p->notes)))
      return false;
    ids[n++] = p->id;
  }
  for (unsigned i = 0; i < d->tasks; i++) {
    const pim_task_t *p = &d->task[i];
    if (!date_ok(p->due) || !date_ok(p->origin) || p->priority < 1 ||
        p->priority > 3 || p->repeat > PIM_YEARLY_SKIP || p->done > 1 ||
        (p->repeat && (!p->due || !p->origin)) ||
        !text(p->uid, sizeof(p->uid)) || !*p->uid ||
        !text(p->title, sizeof(p->title)) || !*p->title ||
        !text(p->category, sizeof(p->category)) ||
        !text(p->notes, sizeof(p->notes)))
      return false;
    ids[n++] = p->id;
  }
  for (unsigned i = 0; i < d->events; i++) {
    const pim_event_t *e = &d->event[i];
    bool found = e->contact == 0;
    for (unsigned j = 0; j < d->contacts; j++)
      if (d->person[j].id == e->contact)
        found = true;
    if (!found)
      return false;
    for (unsigned j = 0; j < i; j++)
      if (!strcmp(e->uid, d->event[j].uid))
        return false;
  }
  for (unsigned i = 0; i < d->contacts; i++)
    for (unsigned j = 0; j < i; j++)
      if (!strcmp(d->person[i].uid, d->person[j].uid))
        return false;
  for (unsigned i = 0; i < d->tasks; i++) {
    const pim_task_t *t = &d->task[i];
    bool found = t->contact == 0;
    for (unsigned j = 0; j < d->contacts; j++)
      if (d->person[j].id == t->contact)
        found = true;
    if (!found ||
        (t->repeat && (t->due < t->origin ||
                       pim_recur_day(t->origin, t->repeat, t->due) != t->due)))
      return false;
    for (unsigned j = 0; j < i; j++)
      if (!strcmp(t->uid, d->task[j].uid))
        return false;
  }
  for (unsigned i = 0; i < n; i++) {
    if (!ids[i] || ids[i] >= d->next_id)
      return false;
    for (unsigned j = 0; j < i; j++)
      if (ids[i] == ids[j])
        return false;
  }
  return true;
}
bool pim_remove(pim_db_t *d, unsigned kind, uint32_t id) {
  if (kind == 0) {
    for (unsigned i = 0; i < d->events; i++)
      if (d->event[i].id == id) {
        memmove(&d->event[i], &d->event[i + 1],
                (--d->events - i) * sizeof(d->event[0]));
        memset(&d->event[d->events], 0, sizeof(d->event[0]));
        return true;
      }
  } else if (kind == 1) {
    for (unsigned i = 0; i < d->contacts; i++)
      if (d->person[i].id == id) {
        memmove(&d->person[i], &d->person[i + 1],
                (--d->contacts - i) * sizeof(d->person[0]));
        memset(&d->person[d->contacts], 0, sizeof(d->person[0]));
        for (unsigned j = 0; j < d->events; j++)
          if (d->event[j].contact == id)
            d->event[j].contact = 0;
        for (unsigned j = 0; j < d->tasks; j++)
          if (d->task[j].contact == id)
            d->task[j].contact = 0;
        return true;
      }
  } else {
    for (unsigned i = 0; i < d->tasks; i++)
      if (d->task[i].id == id) {
        memmove(&d->task[i], &d->task[i + 1],
                (--d->tasks - i) * sizeof(d->task[0]));
        memset(&d->task[d->tasks], 0, sizeof(d->task[0]));
        return true;
      }
  }
  return false;
}
uint32_t pim_crc(const void *data, size_t n) {
  const uint8_t *p = data;
  uint32_t crc = ~0U;
  while (n--) {
    crc ^= *p++;
    for (unsigned i = 0; i < 8; i++)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
  }
  return ~crc;
}
