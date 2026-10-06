#pragma once
#include "solar_os.h"
typedef enum {SOLAR_OS_JOB_RESOURCE_NET,SOLAR_OS_JOB_RESOURCE_FILE} solar_os_job_resource_type_t;
esp_err_t solar_os_jobs_note_resource(const char *,solar_os_job_resource_type_t,const char *,const char *);
esp_err_t solar_os_jobs_get_generation(const char *,uint32_t *);
esp_err_t solar_os_jobs_mark_stopped(const char *,uint32_t,esp_err_t);
