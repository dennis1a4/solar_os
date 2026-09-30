#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "solar_os.h"
void sk_telnet_init(void);
void sk_telnet_poll(bool allow_accept);
bool sk_telnet_connected(void);
int sk_telnet_read(void);
bool sk_telnet_write(const uint8_t *, size_t);
void sk_telnet_disconnect(void);
void sk_telnet_dimensions(uint16_t *, uint16_t *);
void solar_os_shell_cmd_telnetd(solar_os_context_t *, int, char **);
bool sk_console_is_remote(void);
#ifdef __cplusplus
}
#endif
