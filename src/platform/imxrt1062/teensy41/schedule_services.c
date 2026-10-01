#if SK_BACKGROUND_JOBS
#include "solar_os_time.h"
#include "solar_os_rtc.h"
#include "solar_os_task.h"
#include <string.h>
#include <time.h>

/* RTC calendar is usable; wake/alarm IRQ programming is intentionally absent. */
esp_err_t solar_os_rtc_get_info(solar_os_rtc_info_t *out) {
    if(!out)return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));strcpy(out->provider,"teensy-rtc");
    out->capabilities=SOLAR_OS_RTC_CAP_CALENDAR;
    out->interrupt_gpio=SOLAR_OS_RTC_INTERRUPT_GPIO_NONE;return ESP_OK;
}
esp_err_t solar_os_rtc_set_alarm_for(const char *owner,const solar_os_rtc_alarm_t *alarm) {(void)owner;(void)alarm;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t solar_os_rtc_disable_alarm_for(const char *owner) {(void)owner;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t solar_os_rtc_set_countdown_for(const char *owner,uint32_t seconds,bool repeat) {(void)owner;(void)seconds;(void)repeat;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t solar_os_rtc_disable_countdown_for(const char *owner) {(void)owner;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t solar_os_rtc_get_interrupt_status(uint32_t *out) {if(out)*out=0;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t solar_os_rtc_clear_interrupt_status(uint32_t flags) {(void)flags;return ESP_ERR_NOT_SUPPORTED;}
static void from_tm(solar_os_datetime_t *out,const struct tm *t) {
    memset(out,0,sizeof(*out));out->year=t->tm_year+1900;out->month=t->tm_mon+1;
    out->day=t->tm_mday;out->hour=t->tm_hour;out->minute=t->tm_min;out->second=t->tm_sec;
    out->weekday=t->tm_wday;out->clock_integrity=true;
}
esp_err_t solar_os_time_local_to_utc(const solar_os_datetime_t *in,solar_os_datetime_t *out) {
    if(!out || !solar_os_time_datetime_is_valid(in))return ESP_ERR_INVALID_ARG;
    solar_os_time_get_timezone(NULL,0,NULL,0);
    struct tm t={.tm_year=in->year-1900,.tm_mon=in->month-1,.tm_mday=in->day,
        .tm_hour=in->hour,.tm_min=in->minute,.tm_sec=in->second,.tm_isdst=-1};
    time_t value=mktime(&t);struct tm utc;
    if(value<946684800 || (uint64_t)value>UINT32_MAX || t.tm_year!=in->year-1900 ||
        t.tm_mon!=in->month-1 || t.tm_mday!=in->day || t.tm_hour!=in->hour ||
        t.tm_min!=in->minute || t.tm_sec!=in->second || !gmtime_r(&value,&utc))return ESP_ERR_INVALID_ARG;
    from_tm(out,&utc);return ESP_OK;
}
esp_err_t solar_os_time_utc_to_local(const solar_os_datetime_t *in,solar_os_datetime_t *out) {
    if(!out || !solar_os_time_datetime_is_valid(in))return ESP_ERR_INVALID_ARG;
    // Gregorian civil date to Unix days, without changing the process timezone.
    int y=in->year-(in->month<=2),era=y/400;
    unsigned yo=y-era*400,month=in->month>2?in->month-3:in->month+9;
    int64_t days=era*146097+(yo*365+yo/4-yo/100+(153*month+2)/5+in->day-1)-719468;
    int64_t seconds=days*86400+in->hour*3600+in->minute*60+in->second;
    if(seconds<946684800 || seconds>UINT32_MAX)return ESP_ERR_INVALID_ARG;
    solar_os_time_get_timezone(NULL,0,NULL,0);time_t value=seconds;struct tm t;
    if(!localtime_r(&value,&t))return ESP_ERR_INVALID_ARG;
    from_tm(out,&t);return ESP_OK;
}
bool solar_os_task_can_create(uint32_t bytes,solar_os_task_role_t role,bool external) {
    return solar_os_task_admit("job",bytes,role,external);
}
/* Registered Teensy script jobs are cooperative and request no worker stacks. */
void solar_os_task_note_wait_queued(void) {}
void solar_os_task_note_wait_finished(bool launched) {(void)launched;}
#endif
