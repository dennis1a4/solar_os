#include "solar_os_organizer_io.h"
#include "solar_os_organizer_model.h"
#include "solar_os_puzzles.h"
#include "solar_os_units.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static pim_db_t *new_db(void) {
  pim_db_t *d = malloc(sizeof(*d));
  assert(d);
  pim_init(d, "test-device");
  return d;
}
static void dates(void) {
  assert(pim_day(1900, 1, 1) == 1);
  assert(pim_weekday(pim_day(1900, 1, 1)) == 0);
  for (int y = 1900; y <= 2199; y++)
    for (unsigned m = 1; m <= 12; m++)
      for (unsigned day = 1; day <= pim_month_days(y, m); day++) {
        int yy;
        unsigned mm, dd;
        pim_civil(pim_day(y, m, day), &yy, &mm, &dd);
        assert(yy == y && mm == m && dd == day);
      }
  assert(!pim_day(1900, 2, 29) && pim_day(2000, 2, 29) &&
         !pim_day(2100, 2, 29));
  int32_t d;
  assert(!pim_date_parse("2026-02-30", &d));
  assert(!pim_date_parse("2026-1-01", &d));
  assert(pim_date_parse("1970-01-01", &d));
  assert(pim_recur_day(pim_day(2026, 1, 31), PIM_MONTHLY,
                       pim_day(2026, 2, 1)) == pim_day(2026, 2, 28));
  assert(pim_recur_day(pim_day(2026, 1, 31), PIM_MONTHLY,
                       pim_day(2026, 3, 1)) == pim_day(2026, 3, 31));
  assert(pim_recur_day(pim_day(2024, 2, 29), PIM_YEARLY, pim_day(2025, 1, 1)) ==
         pim_day(2025, 2, 28));
  pim_task_t t = {.due = pim_day(2026, 1, 31),
                  .origin = pim_day(2026, 1, 31),
                  .repeat = PIM_MONTHLY};
  assert(pim_task_complete(&t, pim_day(2026, 2, 1)) &&
         t.due == pim_day(2026, 2, 28));
  assert(pim_task_complete(&t, pim_day(2026, 2, 28)) &&
         t.due == pim_day(2026, 3, 31));
}
static void round_trip(void) {
  pim_db_t *a = new_db(), *b = new_db();
  a->events = 1;
  pim_event_t *e = &a->event[0];
  e->id = pim_new_id(a, e->uid);
  e->day = pim_day(2026, 10, 26);
  e->minute = 600;
  e->duration = 90;
  e->repeat = PIM_WEEKLY;
  e->reminder = 15;
  strcpy(e->title, "Meeting, semicolon; and \\backslash");
  strcpy(e->notes, "Long note\nSecond line. \xc3\xa9 UTF8.");
  strcpy(e->category, "Work");
  a->contacts = 1;
  pim_contact_t *p = &a->person[0];
  p->id = pim_new_id(a, p->uid);
  strcpy(p->name, "Person Test");
  strcpy(p->phone, "555-0100");
  strcpy(p->work_phone, "555-0101");
  strcpy(p->email, "local@example.invalid");
  strcpy(p->address, "A street\nCity");
  p->birthday = pim_day(1970, 1, 1);
  a->tasks = 1;
  pim_task_t *t = &a->task[0];
  t->id = pim_new_id(a, t->uid);
  strcpy(t->title, "Check probe");
  t->priority = 1;
  t->due = t->origin = pim_day(2026, 10, 27);
  t->repeat = PIM_DAILY;
  assert(pim_valid(a));
  char error[160];
  unsigned count;
  FILE *f = tmpfile();
  assert(f && pim_write(f, a));
  rewind(f);
  assert(pim_read(f, b, error, sizeof(error)));
  assert(!memcmp(a, b, sizeof(*a)));
  rewind(f);
  fputc('X', f);
  rewind(f);
  assert(!pim_read(f, b, error, sizeof(error)));
  fclose(f);
  for (unsigned kind = 0; kind < 3; kind++) {
    pim_init(b, "other");
    f = tmpfile();
    assert(f && pim_export(f, a, kind));
    rewind(f);
    assert(pim_import(f, b, kind, &count, error, sizeof(error)));
    assert(count == 1);
    rewind(f);
    assert(pim_import(f, b, kind, &count, error, sizeof(error)));
    assert((kind == 0 ? b->events : kind == 1 ? b->contacts : b->tasks) == 1);
    if (kind == 0) {
      assert(!strcmp(b->event[0].title, e->title) &&
             !strcmp(b->event[0].notes, e->notes) &&
             b->event[0].duration == 90 && b->event[0].reminder == 15);
    }
    if (kind == 1)
      assert(!strcmp(b->person[0].address, p->address) &&
             b->person[0].birthday == p->birthday);
    fclose(f);
  }
  for (unsigned policy = PIM_MONTHLY; policy <= PIM_YEARLY_SKIP; policy++) {
    e->day = policy == PIM_YEARLY || policy == PIM_YEARLY_SKIP
                 ? pim_day(2024, 2, 29) : pim_day(2026, 1, 31);
    e->repeat = policy;
    pim_init(b, "roundtrip");
    f = tmpfile();
    assert(f && pim_export(f, a, 0));
    rewind(f);
    assert(pim_import(f, b, 0, &count, error, sizeof(error)));
    assert(b->event[0].repeat == policy);
    fclose(f);
  }
  const char *bad =
      "BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:x\nSUMMARY:bad\nDTSTART:"
      "20261026T100000Z\nEND:VEVENT\nEND:VCALENDAR\n";
  f = tmpfile();
  fputs(bad, f);
  rewind(f);
  pim_init(b, "other");
  assert(!pim_import(f, b, 0, &count, error, sizeof(error)));
  fclose(f);
  assert(pim_remove(a, 1, p->id));
  assert(pim_valid(a));
  a->event[0].id = 0;
  assert(!pim_valid(a));
  free(a);
  free(b);
}
static void units(void) {
  double r;
  char error[128];
  assert(solar_units_convert(32, "F", "C", &r, error, sizeof(error)) &&
         fabs(r) < 1e-10);
  assert(solar_units_convert(1, "in", "mm", &r, error, sizeof(error)) &&
         fabs(r - 25.4) < 1e-9);
  assert(solar_units_convert(1, "MiB", "byte", &r, error, sizeof(error)) &&
         r == 1048576);
  assert(solar_units_convert(1, "Mohm", "mohm", &r, error, sizeof(error)) &&
         r == 1e9);
  assert(!solar_units_convert(-1, "K", "C", &r, error, sizeof(error)));
  assert(!solar_units_convert(1, "m", "kg", &r, error, sizeof(error)));
  assert(!solar_units_convert(NAN, "m", "ft", &r, error, sizeof(error)));
  assert(!solar_units_convert(1, "unknown", "m", &r, error, sizeof(error)));
  for (size_t i = 0; i < solar_units_count(); i++) {
    const solar_unit_t *u = solar_units_get(i);
    assert(solar_units_convert(300, u->symbol, u->symbol, &r, error,
                               sizeof(error)) &&
           fabs(r - 300) < 1e-9);
  }
}
static void puzzles(void) {
  for (unsigned seed = 1; seed < 9; seed++) {
    solar_sudoku_t s;
    solar_sudoku_new(&s, seed);
    uint8_t copy[81];
    memcpy(copy, s.value, 81);
    assert(solar_sudoku_solutions(copy, 2) == 1);
    unsigned givens = 0;
    for (unsigned i = 0; i < 81; i++) {
      givens += s.given[i];
      if (s.given[i])
        assert(!solar_sudoku_set(&s, i, 0));
    }
    assert(givens >= 36 && givens < 81);
    memcpy(s.value, s.solution, 81);
    assert(solar_sudoku_solved(&s));
    s.value[0] = s.value[1];
    assert(solar_sudoku_conflict(&s, 0) && !solar_sudoku_solved(&s));
  }
  for (unsigned safe = 0; safe < 81; safe++) {
    solar_mines_t m;
    solar_mines_new(&m, safe + 1);
    assert(solar_mines_reveal(&m, safe) && !m.lost);
    assert(!m.bomb[safe] && !m.around[safe]);
    unsigned bombs = 0;
    for (unsigned i = 0; i < 81; i++)
      bombs += m.bomb[i];
    assert(bombs == 10);
    for (unsigned i = 0; i < 81; i++)
      if (!m.bomb[i])
        solar_mines_reveal(&m, i);
    assert(m.won && !m.lost);
    assert(!solar_mines_flag(&m, 0));
  }
  solar_mines_t m;
  solar_mines_new(&m, 42);
  assert(solar_mines_flag(&m, 3) && !solar_mines_reveal(&m, 3));
  solar_mines_flag(&m, 3);
  solar_mines_reveal(&m, 3);
  for (unsigned i = 0; i < 81; i++)
    if (m.bomb[i]) {
      solar_mines_reveal(&m, i);
      break;
    }
  assert(m.lost && !m.won);
}
int main(void) {
  assert(pim_recur_day(pim_day(2026, 1, 31), PIM_MONTHLY_SKIP, pim_day(2026, 2, 1)) == pim_day(2026, 3, 31));
  assert(pim_recur_day(pim_day(2024, 2, 29), PIM_YEARLY_SKIP, pim_day(2025, 1, 1)) == pim_day(2028, 2, 29));
  dates();
  round_trip();
  units();
  puzzles();
  puts("PDA core: civil dates/recurrence, tasks, CRC persistence, ICS/vCard "
       "merge/escaping, dimensional conversions, unique Sudoku and safe-first "
       "Mines passed");
}
