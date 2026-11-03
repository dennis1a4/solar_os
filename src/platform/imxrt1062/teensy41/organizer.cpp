#if SK_ORGANIZER
#include <arduino_freertos.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <semphr.h>
#include <sys/stat.h>
extern "C" {
#include "solar_os_memory.h"
#include "solar_os_organizer.h"
#include "solar_os_organizer_io.h"
#include "solar_os_schedule.h"
#include "solar_os_storage.h"
#include "solar_os_task.h"
#include "solar_os_time.h"
#include "solar_os_writer_files.h"
}
static StaticSemaphore_t mutex_buffer;
static SemaphoreHandle_t gate;
static pim_db_t *database;
static char store[128], state[128];
static bool ready, writable, alert;
static uint32_t alarm_at;
static char alert_title[81], next_title[81], next_time[64];
static uint32_t last_poll;
static TaskHandle_t organizer_worker;
static void run_organizer(void *) {
  for (;;) {
    solar_os_organizer_poll();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
static bool owns_alarm;
/* Lock order is organizer -> scheduler; scheduler never calls organizer.
 * Keep creation/dismissal serialized so a fast acknowledgement cannot race
 * a queued beep into sounding after the visible reminder was dismissed. */
static void stop_alarm_locked() {
  if (!owns_alarm)
    return;
  solar_os_schedule_entry_t entry{};
  if (solar_os_schedule_get_by_name("_organizer", &entry) == ESP_OK &&
      !entry.persistent && entry.action == SOLAR_OS_SCHEDULE_ACTION_ALARM) {
    char name[32];
    if (solar_os_schedule_alarm_active(name, sizeof(name)) &&
        !strcmp(name, "_organizer"))
      solar_os_schedule_stop_alarm();
    solar_os_schedule_remove("_organizer");
  }
  owns_alarm = false;
}
static void start_alarm_locked(const char *title) {
  stop_alarm_locked();
  solar_os_schedule_entry_t entry{};
  if (solar_os_schedule_get_by_name("_organizer", &entry) == ESP_OK) {
    strcpy(state, "Reminder display only: alarm name already in use");
    return;
  }
  owns_alarm = solar_os_schedule_add_relative("_organizer", 1,
                                              SOLAR_OS_SCHEDULE_ACTION_ALARM,
                                              title, false) == ESP_OK;
  if (!owns_alarm)
    strcpy(state, "Reminder display only: alarm slot unavailable");
}
int32_t solar_os_organizer_today(uint16_t *minute) {
  solar_os_datetime_t t{};
  if (solar_os_time_get_datetime(&t) != ESP_OK || !t.clock_integrity ||
      t.year < 2000)
    return 0;
  if (minute)
    *minute = t.hour * 60 + t.minute;
  return pim_day(t.year, t.month, t.day);
}
static bool read_file(const char *path, pim_db_t *d, char *error, size_t n) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  bool ok = pim_read(f, d, error, n);
  if (fclose(f))
    ok = false;
  return ok;
}
static bool write_file(const char *path, const pim_db_t *d, char *error,
                       size_t n) {
  char staged[SOLAR_OS_STORAGE_PATH_MAX], backup[SOLAR_OS_STORAGE_PATH_MAX];
  if (solar_os_storage_sibling_path(path, ".tmp", staged, sizeof(staged)) !=
          ESP_OK ||
      solar_os_storage_sibling_path(path, ".bak", backup, sizeof(backup)) !=
          ESP_OK)
    return false;
  FILE *f = fopen(staged, "wb");
  if (!f) {
    snprintf(error, n, "Cannot create staged organizer file");
    return false;
  }
  bool ok = pim_write(f, d) && solar_os_storage_sync_file(f) == ESP_OK;
  if (fclose(f))
    ok = false;
  if (ok)
    ok = solar_os_storage_replace_file(staged, path, backup) == ESP_OK;
  if (!ok) {
    remove(staged);
    snprintf(error, n, "Save failed; previous organizer retained");
  }
  return ok;
}
void solar_os_organizer_begin() {
  if (ready)
    return;
  if (!gate)
    gate = xSemaphoreCreateMutexStatic(&mutex_buffer);
  if (!gate)
    return;
  xSemaphoreTake(gate, portMAX_DELAY);
  if (ready) {
    xSemaphoreGive(gate);
    return;
  }
  database = static_cast<pim_db_t *>(solar_os_memory_alloc(
      sizeof(*database), SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "organizer"));
  if (!database) {
    strcpy(state, "Organizer requires PSRAM");
    ready = true;
    xSemaphoreGive(gate);
    return;
  }
  char ns[33];
  snprintf(ns, sizeof(ns), "%08lx%08lx%08lx", (unsigned long)HW_OCOTP_MAC0,
           (unsigned long)HW_OCOTP_MAC1, (unsigned long)millis());
  pim_init(database, ns);
  char dir[96];
  writable =
      solar_os_storage_default_path(".organizer", dir, sizeof(dir)) == ESP_OK;
  if (writable) {
    struct stat info{};
    if (stat(dir, &info) != 0)
      writable = mkdir(dir, 0777) == 0;
    else
      writable = S_ISDIR(info.st_mode);
  }
  if (writable) {
    snprintf(store, sizeof(store), "%s/organizer.bin", dir);
    struct stat info{};
    bool exists = stat(store, &info) == 0;
    int lookup_error = errno;
    bool loaded = exists && read_file(store, database, state, sizeof(state));
    if (!loaded) {
      char backup[140];
      snprintf(backup, sizeof(backup), "%s.bak", store);
      if (read_file(backup, database, state, sizeof(state))) {
        strcpy(state, "Recovered staged-save backup");
      } else if (exists) {
        pim_init(database, ns);
        writable = false;
        strcpy(state, "Corrupt organizer: restore backup; original preserved");
      } else if (lookup_error != ENOENT) {
        pim_init(database, ns);
        writable = false;
        strcpy(state, "Organizer unavailable; writes blocked");
      } else {
        pim_init(database, ns);
        state[0] = 0;
      }
    }
  }
  if (writable && !*state)
    strcpy(state, "Saved locally; no account sync");
  else if (!writable && !*state)
    strcpy(state, "Persistent flash/SD unavailable; writes blocked");
  ready = true;
  xSemaphoreGive(gate);
  solar_os_schedule_init();
  if (solar_os_task_create_pinned_external(run_organizer, "organizer", 8192,
                                           nullptr, 1, &organizer_worker, 0,
                                           SOLAR_OS_TASK_ROLE_SYSTEM) != pdPASS)
    snprintf(state, sizeof(state), "Saved; reminder worker unavailable");
}
pim_db_t *solar_os_organizer_lock(char *status, size_t n) {
  solar_os_organizer_begin();
  if (!gate)
    return nullptr;
  xSemaphoreTake(gate, portMAX_DELAY);
  if (status)
    snprintf(status, n, "%s", state);
  return database;
}
void solar_os_organizer_unlock() {
  if (gate)
    xSemaphoreGive(gate);
}
pim_db_t *solar_os_organizer_copy() {
  auto *copy = static_cast<pim_db_t *>(
      solar_os_memory_alloc(sizeof(pim_db_t), SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,
                            "organizer.transaction"));
  if (!copy)
    return nullptr;
  auto *d = solar_os_organizer_lock(nullptr, 0);
  if (d)
    memcpy(copy, d, sizeof(*copy));
  solar_os_organizer_unlock();
  if (!d) {
    solar_os_memory_free(copy);
    return nullptr;
  }
  return copy;
}
bool solar_os_organizer_commit(pim_db_t *copy, uint32_t expected, char *error,
                               size_t n) {
  auto *d = solar_os_organizer_lock(nullptr, 0);
  bool ok = false;
  if (!d || !writable)
    snprintf(error, n, "Persistent store unavailable; no change saved");
  else if (d->generation != expected)
    snprintf(error, n, "Organizer changed while editing; retry");
  else if (!pim_valid(copy))
    snprintf(error, n, "Invalid record; required fields/date/limits");
  else {
    copy->generation = d->generation + 1;
    if (write_file(store, copy, error, n)) {
      memcpy(d, copy, sizeof(*d));
      strcpy(state, "Saved locally");
      snprintf(error, n, "Saved");
      ok = true;
      last_poll = 0;
    }
  }
  solar_os_organizer_unlock();
  return ok;
}
bool solar_os_organizer_transfer(const char *path, unsigned kind,
                                 bool importing, char *error, size_t n) {
  auto *copy = solar_os_organizer_copy();
  if (!copy) {
    snprintf(error, n, "No transaction memory");
    return false;
  }
  uint32_t gen = copy->generation;
  FILE *f = fopen(path, importing ? "rb" : "wx");
  bool ok = false;
  unsigned count = 0;
  if (!f)
    snprintf(error, n, "Cannot open path; exports require a new filename");
  else {
    if (importing)
      ok = pim_import(f, copy, kind, &count, error, n);
    else
      ok = pim_export(f, copy, kind) && solar_os_storage_sync_file(f) == ESP_OK;
    if (fclose(f))
      ok = false;
    if (ok && importing)
      ok = solar_os_organizer_commit(copy, gen, error, n);
    if (ok)
      snprintf(error, n, importing ? "Imported %u records" : "Exported", count);
    else if (!importing) {
      remove(path);
      snprintf(error, n, "Export failed; incomplete output removed");
    }
  }
  solar_os_memory_free(copy);
  return ok;
}
bool solar_os_organizer_backup(const char *path, bool restore, char *error,
                               size_t n) {
  auto *copy = solar_os_organizer_copy();
  if (!copy)
    return false;
  bool ok = false;
  if (!restore) {
    FILE *f = fopen(path, "wx");
    if (f) {
      ok = pim_write(f, copy) && solar_os_storage_sync_file(f) == ESP_OK;
      if (fclose(f))
        ok = false;
      if (!ok)
        remove(path);
    }
    snprintf(error, n,
             ok ? "Backup saved" : "Backup failed; choose a new filename");
  } else if (read_file(path, copy, error, n)) {
    auto *d = solar_os_organizer_lock(nullptr, 0);
    if (d && *store) {
      copy->generation = d->generation + 1;
      if (copy->next_id < d->next_id)
        copy->next_id = d->next_id;
      ok = write_file(store, copy, error, n);
      if (ok) {
        memcpy(d, copy, sizeof(*d));
        writable = true;
        strcpy(state, "Backup restored");
        stop_alarm_locked();
        alert = false;
        alarm_at = 0;
        last_poll = 0;
      }
    }
    solar_os_organizer_unlock();
    snprintf(error, n,
             ok ? "Backup restored" : "Restore failed; original retained");
  } else
    snprintf(error, n, "Invalid or unavailable backup; original retained");
  solar_os_memory_free(copy);
  return ok;
}
void solar_os_organizer_poll() {
  if (!ready || !database)
    return;
  uint32_t now = millis();
  if (last_poll && uint32_t(now - last_poll) < 1000)
    return;
  last_poll = now;
  uint16_t min;
  int32_t today = solar_os_organizer_today(&min);
  if (!today)
    return;
  int64_t current = (int64_t)today * 1440 + min;
  bool sound = false;
  char title[81]{};
  auto *d = solar_os_organizer_lock(nullptr, 0);
  int64_t closest = INT64_MAX;
  next_title[0] = next_time[0] = 0;
  for (unsigned i = 0; i < d->events; i++) {
    auto &e = d->event[i];
    int64_t next = pim_event_next(&e, current);
    if (next >= 0 && next < closest) {
      closest = next;
      strcpy(next_title, e.title);
    }
    if (!alert && e.reminder >= 0) {
      int64_t occurrence = pim_event_next(&e, current - 1440);
      for (unsigned attempt = 0; attempt < 3 && occurrence >= 0; attempt++) {
        if (occurrence <= e.notified) {
          occurrence = pim_event_next(&e, occurrence + 1);
          continue;
        }
        if (current >= occurrence - e.reminder && current < occurrence + 1440) {
          e.notified = occurrence;
          d->generation++;
          alert = true;
          strcpy(alert_title, e.title);
          strcpy(title, e.title);
          sound = true;
          alarm_at = now;
        }
        break;
      }
    }
  }
  if (closest != INT64_MAX) {
    char date[24];
    pim_date_text(closest / 1440, date, sizeof(date));
    snprintf(next_time, sizeof(next_time), "%s %02u:%02u", date,
             unsigned(closest % 1440) / 60, unsigned(closest % 60));
  }
  if (sound)
    start_alarm_locked(title);
  if (alert && alarm_at && uint32_t(now - alarm_at) > 11000) {
    stop_alarm_locked();
    alarm_at = 0;
  }
  solar_os_organizer_unlock();
}
void solar_os_organizer_small(char lines[9][27]) {
  if (!ready || !database)
    return;
  auto *d = solar_os_organizer_lock(nullptr, 0);
  (void)d;
  if (alert) {
    snprintf(lines[6], 27, "REMINDER (Start = dismiss)");
    snprintf(lines[7], 27, "%.26s", alert_title);
  } else if (*next_title) {
    snprintf(lines[6], 27, "Next %.20s", next_time);
    snprintf(lines[7], 27, "%.26s", next_title);
  }
  solar_os_organizer_unlock();
}
void solar_os_organizer_ack() {
  if (!ready || !database)
    return;
  auto *d = solar_os_organizer_lock(nullptr, 0);
  (void)d;
  bool active = alert;
  alert = false;
  alarm_at = 0;
  stop_alarm_locked();
  solar_os_organizer_unlock();
  if (!active)
    return;
  auto *copy = solar_os_organizer_copy();
  if (!copy)
    return;
  char error[128];
  solar_os_organizer_commit(copy, copy->generation, error, sizeof(error));
  solar_os_memory_free(copy);
}

#endif
