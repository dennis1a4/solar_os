#if SK_BACKGROUND_JOBS
/* Retain the port's rtc epoch CLI while reusing the shared schedule parser. */
#define solar_os_shell_cmd_rtc sk_shared_rtc_command
#include "shell/solar_os_shell_schedule.c"
#endif
