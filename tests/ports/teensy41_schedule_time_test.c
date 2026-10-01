#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include "solar_os_time.h"
/* The existing Clock test covers timezone persistence and date validation. */
void solar_os_time_get_timezone(char *a,size_t b,char *c,size_t d) {(void)a;(void)b;(void)c;(void)d;}
bool solar_os_time_datetime_is_valid(const solar_os_datetime_t *v) {
    return v && v->year>=2000 && v->month>=1 && v->month<=12 && v->day>=1 && v->day<=31 && v->hour<24 && v->minute<60 && v->second<60;
}
int main(void) {
    solar_os_datetime_t in={.year=2026,.month=9,.day=30,.hour=12},utc,back;
    setenv("TZ","CST6CDT,M3.2.0,M11.1.0",1);tzset();
    assert(solar_os_time_local_to_utc(&in,&utc)==ESP_OK && utc.hour==17);
    assert(solar_os_time_utc_to_local(&utc,&back)==ESP_OK && back.hour==12 && back.day==30);
    in.month=1;assert(solar_os_time_local_to_utc(&in,&utc)==ESP_OK && utc.hour==18);
    in.month=3;in.day=8;in.hour=2;in.minute=30;
    assert(solar_os_time_local_to_utc(&in,&utc)==ESP_ERR_INVALID_ARG);
    setenv("TZ","UTC0",1);tzset();
    in=(solar_os_datetime_t){.year=2106,.month=2,.day=7,.hour=6,.minute=28,.second=15};
    assert(solar_os_time_local_to_utc(&in,&utc)==ESP_OK);
    assert(solar_os_time_utc_to_local(&in,&back)==ESP_OK);
    in.second=16;assert(solar_os_time_local_to_utc(&in,&utc)==ESP_ERR_INVALID_ARG);
    assert(solar_os_time_utc_to_local(&in,&back)==ESP_ERR_INVALID_ARG);
    assert(solar_os_time_local_to_utc(NULL,&utc)==ESP_ERR_INVALID_ARG);
    return 0;
}
