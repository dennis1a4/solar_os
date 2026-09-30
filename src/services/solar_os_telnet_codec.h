#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Bounded streaming adaptation of jobs/solar_os_telnetd_job.c negotiation/NVT
 * handling. No sockets or allocation; callbacks return false on transport loss. */
typedef struct {
    unsigned state, option;
    uint8_t sub[40]; size_t length;
    bool overflow, suppress, tx_cr;
    uint16_t cols, rows;
    char terminal[32];
    bool (*wire)(void *, const uint8_t *, size_t);
    bool (*data)(void *, uint8_t);
    void *user;
} solar_telnet_codec_t;
void solar_telnet_codec_init(solar_telnet_codec_t *, bool (*)(void *, const uint8_t *, size_t), bool (*)(void *, uint8_t), void *);
bool solar_telnet_negotiate(solar_telnet_codec_t *);
bool solar_telnet_feed(solar_telnet_codec_t *, uint8_t);
bool solar_telnet_write(solar_telnet_codec_t *, const uint8_t *, size_t);
#ifdef __cplusplus
}
#endif
