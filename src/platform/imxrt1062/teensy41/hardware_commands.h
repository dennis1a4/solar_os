#pragma once
#include "solar_os.h"
#include "solar_os_completion.h"
#ifdef __cplusplus
extern "C" {
#endif
void solar_os_shell_cmd_spi(solar_os_context_t *,int,char **);
void solar_os_shell_cmd_expansion(solar_os_context_t *,int,char **);
void sk_shell_cmd_serial(solar_os_context_t *,int,char **);
void sk_shell_cmd_io(solar_os_context_t *,int,char **);
void sk_hardware_console_release(void);
bool sk_hardware_complete(const solar_os_completion_request_t *,solar_os_completion_emit_t,void *,void *);
#ifdef __cplusplus
}
#endif
