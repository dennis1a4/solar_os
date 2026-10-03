#pragma once
#include <stddef.h>
#include "solar_os_pd_power.h"
/* One task owns this backend. read/write must be bounded and bus-locked.
 * Service receive alerts promptly (source PDOs can be overwritten in ~3 ms).
 * No controller NVM writes; no automatic high-voltage requests. */
typedef struct {
    bool (*read)(void *,uint8_t,uint8_t *,size_t);
    bool (*write)(void *,uint8_t,const uint8_t *,size_t);
    void *user;
} solar_stusb4500_io_t;
typedef struct {
    solar_stusb4500_io_t io;
    solar_pd_power_snapshot_t source;
    uint32_t raw_pdos[7];
    unsigned raw_count;
    uint32_t began;
    uint16_t max_mv,max_ma;
    solar_pd_power_profile_t requested;
    bool initialized,pending,fresh_caps,accepted,ps_ready;
} solar_stusb4500_t;
bool solar_stusb4500_init(solar_stusb4500_t *,const solar_stusb4500_io_t *,uint16_t,uint16_t);
extern const solar_pd_power_backend_t solar_stusb4500_backend;
