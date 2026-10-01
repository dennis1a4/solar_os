#if SK_CLOCK
/* RTC/timezone adapter. Full display builds use the shared schedule service;
 * focused Clock builds retain the transient-only fallback below. */
#include "esp_timer.h"
#include "nvs.h"
#include "solar_os_schedule.h"
#include "solar_os_time.h"
#include "solar_os_timezone.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern uint32_t sk_clock_rtc_epoch(void);
extern void sk_clock_rtc_set(uint32_t epoch);
extern void sk_clock_alarm_sound(bool on);
static char timezone_name[SOLAR_OS_TIMEZONE_NAME_MAX] = "UTC";
static char timezone_posix[SOLAR_OS_TIMEZONE_POSIX_MAX] = "UTC0";
static bool timezone_loaded;
static void timezone_load(void) {
    if (timezone_loaded)
        return;
    timezone_loaded = true;
    nvs_handle_t h;
    if (nvs_open("time", NVS_READONLY, &h) == ESP_OK) {
        char name[sizeof(timezone_name)], posix[sizeof(timezone_posix)];
        size_t a = sizeof(name), b = sizeof(posix);
        if (nvs_get_str(h, "tz_name", name, &a) == ESP_OK &&
            nvs_get_str(h, "tz_posix", posix, &b) == ESP_OK &&
            solar_os_timezone_value_is_raw_posix(posix)) {
            strcpy(timezone_name, name);
            strcpy(timezone_posix, posix);
        }
        nvs_close(h);
    }
    setenv("TZ", timezone_posix, 1);
    tzset();
}
void solar_os_time_get_timezone(char *name, size_t a, char *posix, size_t b) {
    timezone_load();
    if (name && a)
        snprintf(name, a, "%s", timezone_name);
    if (posix && b)
        snprintf(posix, b, "%s", timezone_posix);
}
esp_err_t solar_os_time_set_timezone(const char *value) {
    char name[sizeof(timezone_name)], posix[sizeof(timezone_posix)];
    if (!solar_os_timezone_resolve(value, name, sizeof(name), posix, sizeof(posix)))
        return ESP_ERR_INVALID_ARG;
    timezone_load();
    nvs_handle_t h;
    esp_err_t err = nvs_open("time", NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;
    err = nvs_set_str(h, "tz_name", name);
    if (err == ESP_OK)
        err = nvs_set_str(h, "tz_posix", posix);
    if (err == ESP_OK && setenv("TZ", posix, 1) != 0)
        err = ESP_ERR_NO_MEM;
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        strcpy(timezone_name, name);
        strcpy(timezone_posix, posix);
    } else
        setenv("TZ", timezone_posix, 1);
    tzset();
    return err;
}
#if !SK_BACKGROUND_JOBS
static struct {
    bool exists, ringing, silenced;
    uint64_t deadline, next_tone;
} alarm;
static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000U; }
static bool clock_name(const char *name) { return name && !strcmp(name, "_clock"); }

#endif

bool solar_os_time_datetime_is_valid(const solar_os_datetime_t *d) {
    if (!d || d->year < 2000 || d->month < 1 || d->month > 12 || !d->day || d->hour > 23 ||
        d->minute > 59 || d->second > 59 || d->weekday > 6)
        return false;
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned limit = days[d->month - 1];
    if (d->month == 2 && d->year % 4 == 0 && (d->year % 100 != 0 || d->year % 400 == 0))
        ++limit;
    return d->day <= limit;
}
esp_err_t solar_os_time_get_datetime(solar_os_datetime_t *d) {
    if (!d)
        return ESP_ERR_INVALID_ARG;
    memset(d, 0, sizeof(*d));
    time_t epoch = (time_t)sk_clock_rtc_epoch();
    timezone_load();
    struct tm utc; /* Local display; the hardware RTC remains UTC. */
    if (epoch < 946684800 || !localtime_r(&epoch, &utc))
        return ESP_ERR_INVALID_STATE;
    d->year = utc.tm_year + 1900;
    d->month = utc.tm_mon + 1;
    d->day = utc.tm_mday;
    d->hour = utc.tm_hour;
    d->minute = utc.tm_min;
    d->second = utc.tm_sec;
    d->weekday = utc.tm_wday;
    d->clock_integrity = true;
    return solar_os_time_datetime_is_valid(d) ? ESP_OK : ESP_ERR_INVALID_STATE;
}
esp_err_t solar_os_time_set_datetime(const solar_os_datetime_t *d) {
    if (!solar_os_time_datetime_is_valid(d))
        return ESP_ERR_INVALID_ARG;
    timezone_load();
    struct tm local = {.tm_year=d->year-1900, .tm_mon=d->month-1, .tm_mday=d->day,
        .tm_hour=d->hour, .tm_min=d->minute, .tm_sec=d->second, .tm_isdst=-1};
    time_t epoch=mktime(&local);
    /* Reject RTC overflow and nonexistent local times normalized by mktime. */
    if (epoch < 946684800 || (uint64_t)epoch > UINT32_MAX ||
        local.tm_year != d->year-1900 || local.tm_mon != d->month-1 ||
        local.tm_mday != d->day || local.tm_hour != d->hour ||
        local.tm_min != d->minute || local.tm_sec != d->second)
        return ESP_ERR_INVALID_ARG;
    sk_clock_rtc_set((uint32_t)epoch);
    return ESP_OK;
}
#if !SK_BACKGROUND_JOBS
esp_err_t solar_os_schedule_add_relative(const char *name, uint32_t seconds,
                                         solar_os_schedule_action_t action, const char *value,
                                         bool persistent) {
    if (!clock_name(name) || !seconds || seconds > 5999 ||
        action != SOLAR_OS_SCHEDULE_ACTION_ALARM || (value && *value) || persistent)
        return ESP_ERR_INVALID_ARG;
    if (alarm.exists)
        return ESP_ERR_INVALID_STATE;
    alarm.exists = true;
    alarm.ringing = false;
    alarm.silenced = false;
    alarm.deadline = now_ms() + (uint64_t)seconds * 1000;
    alarm.next_tone = 0;
    return ESP_OK;
}
void solar_os_schedule_poll(void) {
    if (!alarm.exists || alarm.silenced)
        return;
    uint64_t now = now_ms();
    if (now < alarm.deadline)
        return;
    alarm.ringing = true;
    if (now >= alarm.next_tone) {
        alarm.next_tone = now + 1600;
        sk_clock_alarm_sound(true);
    }
}
esp_err_t solar_os_schedule_remaining_seconds(const char *name, uint32_t *seconds) {
    if (!clock_name(name) || !seconds)
        return ESP_ERR_INVALID_ARG;
    if (!alarm.exists)
        return ESP_ERR_NOT_FOUND;
    uint64_t now = now_ms();
    *seconds = now >= alarm.deadline ? 0 : (uint32_t)((alarm.deadline - now + 999) / 1000);
    return ESP_OK;
}
bool solar_os_schedule_alarm_active(char *name, size_t size) {
    if (!alarm.ringing)
        return false;
    if (name && size) {
        size_t n = size - 1;
        if (n > 6)
            n = 6;
        memcpy(name, "_clock", n);
        name[n] = 0;
    }
    return true;
}
void solar_os_schedule_stop_alarm(void) {
    alarm.ringing = false;
    /* Keep zero on screen without rearming an expired countdown. */
    alarm.silenced = true;
    sk_clock_alarm_sound(false);
}
esp_err_t solar_os_schedule_remove(const char *name) {
    if (!clock_name(name))
        return ESP_ERR_INVALID_ARG;
    if (!alarm.exists)
        return ESP_ERR_NOT_FOUND;
    sk_clock_alarm_sound(false);
    memset(&alarm, 0, sizeof(alarm));
    return ESP_OK;
}
#endif
#endif
