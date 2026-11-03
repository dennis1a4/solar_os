#pragma once
#include "esp_err.h"
#include "solar_os_dnp3_lab.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct solar_os_dnp3 solar_os_dnp3_t;
typedef struct {
  bool running, connected[2];
  char detail[128];
  uint32_t worker_stack_free;
} solar_os_dnp3_status_t;
esp_err_t solar_os_dnp3_create(solar_os_dnp3_t **);
void solar_os_dnp3_halt(solar_os_dnp3_t *);
void solar_os_dnp3_destroy(solar_os_dnp3_t *);
bool solar_os_dnp3_command(solar_os_dnp3_t *, const char *, char *, size_t);
dnp_lab_t *solar_os_dnp3_lock(solar_os_dnp3_t *, solar_os_dnp3_status_t *);
void solar_os_dnp3_unlock(solar_os_dnp3_t *);
#ifdef __cplusplus
}
#endif
