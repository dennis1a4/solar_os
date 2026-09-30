#pragma once
#include "solar_os_obd.h"
typedef enum {
    OBD_DEMO_NORMAL,
    OBD_DEMO_TIMEOUT,
    OBD_DEMO_SEQUENCE,
    OBD_DEMO_REJECT
} solar_obd_demo_mode_t;
typedef struct {
    solar_can_t *bus;
    solar_isotp_t ecu[2];
    solar_obd_demo_mode_t mode;
    bool cleared[2];
} solar_obd_demo_t;
void solar_obd_demo_init(solar_obd_demo_t *d, solar_can_t *bus, solar_obd_demo_mode_t mode);
void solar_obd_demo_poll(solar_obd_demo_t *d, uint32_t now);
