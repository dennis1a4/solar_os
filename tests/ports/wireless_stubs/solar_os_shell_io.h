#pragma once
#include "solar_os.h"
typedef struct solar_os_shell_io {int command_status;} solar_os_shell_io_t;
static inline solar_os_shell_io_t *solar_os_context_shell_io(solar_os_context_t *c){return c->io;}
void solar_os_shell_io_writeln(solar_os_shell_io_t *,const char *);
void solar_os_shell_io_printf(solar_os_shell_io_t *,const char *,...);
