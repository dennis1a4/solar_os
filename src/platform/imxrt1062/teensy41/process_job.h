#pragma once
#include "solar_os.h"
#include "solar_os_shell_io.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t sk_process_start(solar_os_context_t *,esp_err_t (*)(solar_os_context_t *),bool (*)(solar_os_context_t *,const solar_os_event_t *),void (*)(solar_os_context_t *));
void sk_process_stop(solar_os_context_t *);
bool sk_process_event(solar_os_context_t *,const solar_os_event_t *);
void sk_process_suspend(solar_os_context_t *);
void sk_process_resume(solar_os_context_t *);
bool sk_process_poll_cancel(void);
int sk_process_stdin(void);
bool sk_process_graphics_allowed(void);
#ifdef __cplusplus
}
#endif
