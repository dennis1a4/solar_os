#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PIM_EVENTS 256
#define PIM_CONTACTS 512
#define PIM_TASKS 256
#define PIM_UID 96
/* Civil dates are days since 1900-01-01 plus one; zero is reserved for an
 * absent date. Appointment times float in the configured local time zone. */
enum { PIM_NONE, PIM_DAILY, PIM_WEEKLY, PIM_MONTHLY, PIM_YEARLY, PIM_MONTHLY_SKIP, PIM_YEARLY_SKIP };
typedef struct {
  uint32_t id;
  int32_t day;
  uint16_t minute, duration;
  int16_t reminder;
  uint8_t repeat, all_day;
  uint32_t contact;
  int64_t notified;
  char uid[PIM_UID], title[81], category[33], location[81], notes[257];
} pim_event_t;
typedef struct {
  uint32_t id;
  int32_t birthday;
  char uid[PIM_UID], name[81], company[81], phone[41], work_phone[41],
      email[97], address[161], category[33], notes[257];
} pim_contact_t;
typedef struct {
  uint32_t id;
  int32_t due, origin;
  uint32_t contact, completed;
  uint8_t priority, repeat, done, reserved;
  char uid[PIM_UID], title[81], category[33], notes[257];
} pim_task_t;
typedef struct {
  uint32_t next_id, generation;
  uint16_t events, contacts, tasks, reserved;
  char namespace_id[33];
  pim_event_t event[PIM_EVENTS];
  pim_contact_t person[PIM_CONTACTS];
  pim_task_t task[PIM_TASKS];
} pim_db_t;
void pim_init(pim_db_t *, const char *namespace_id);
int32_t pim_day(int year, unsigned month, unsigned day);
void pim_civil(int32_t, int *year, unsigned *month, unsigned *day);
unsigned pim_month_days(int, unsigned);
int pim_weekday(int32_t); /* Monday=0 */
bool pim_date_parse(const char *, int32_t *);
void pim_date_text(int32_t, char *, size_t);
bool pim_time_parse(const char *, uint16_t *);
bool pim_contains(const char *, const char *);
int32_t pim_recur_day(int32_t origin, unsigned repeat, int32_t on_or_after);
int64_t pim_event_next(const pim_event_t *, int64_t on_or_after);
bool pim_task_complete(pim_task_t *, int32_t today);
bool pim_valid(const pim_db_t *);
uint32_t pim_new_id(pim_db_t *, char uid[PIM_UID]);
bool pim_remove(pim_db_t *, unsigned kind, uint32_t id);
uint32_t pim_crc(const void *, size_t);
