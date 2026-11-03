#pragma once
#include "esp_err.h"
#include "solar_os_organizer_model.h"
#ifdef __cplusplus
extern "C" {
#endif
void solar_os_organizer_begin(void);
pim_db_t *solar_os_organizer_lock(char *status, size_t);
void solar_os_organizer_unlock(void);
pim_db_t *solar_os_organizer_copy(void);
bool solar_os_organizer_commit(pim_db_t *, uint32_t expected_generation, char *,
                               size_t);
bool solar_os_organizer_transfer(const char *path, unsigned kind, bool import,
                                 char *, size_t);
bool solar_os_organizer_backup(const char *, bool restore, char *, size_t);
void solar_os_organizer_poll(void);
void solar_os_organizer_small(char lines[9][27]);
void solar_os_organizer_ack(void);
int32_t solar_os_organizer_today(uint16_t *minute);
/* Diagnostic NES input follows the same ownership rule as physical controls. */
bool solar_os_puzzle_nes_claim(const void *owner, bool enable);
uint8_t solar_os_puzzle_nes_read(const void *owner);
#ifdef __cplusplus
}
#endif
