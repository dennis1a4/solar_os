#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SOLAR_PD_POWER_PROFILES 7
/* Fixed-supply profiles only. Current is a contract limit, not a measurement. */
typedef struct { uint16_t mv, ma; } solar_pd_power_profile_t;
typedef enum {
    SOLAR_PD_POWER_IDLE, SOLAR_PD_POWER_WAITING, SOLAR_PD_POWER_READY,
    SOLAR_PD_POWER_REJECTED, SOLAR_PD_POWER_TIMEOUT, SOLAR_PD_POWER_DISCONNECTED,
    SOLAR_PD_POWER_IO_ERROR, SOLAR_PD_POWER_MISMATCH
} solar_pd_power_state_t;
typedef struct {
    bool attached, contract_valid;
    unsigned count;
    solar_pd_power_profile_t profiles[SOLAR_PD_POWER_PROFILES], contract;
} solar_pd_power_snapshot_t;
typedef enum { SOLAR_PD_POWER_PENDING, SOLAR_PD_POWER_ACCEPTED, SOLAR_PD_POWER_DENIED,
               SOLAR_PD_POWER_DETACHED, SOLAR_PD_POWER_FAILED } solar_pd_power_result_t;
/* Hardware backend must report ACCEPTED only after a fresh PS_READY/contract
 * observation for this request, never from a stale RDO or a successful write.
 * Calls must be bounded; poll must also report idle detach/contract changes. */
typedef struct {
    bool (*request)(void *, solar_pd_power_profile_t, uint32_t);
    solar_pd_power_result_t (*poll)(void *, uint32_t, solar_pd_power_snapshot_t *);
} solar_pd_power_backend_t;
typedef struct {
    const solar_pd_power_backend_t *backend;
    void *user;
    uint16_t max_mv, max_ma;
    solar_pd_power_snapshot_t source;
    solar_pd_power_profile_t requested;
    solar_pd_power_state_t state;
    uint32_t began;
} solar_pd_power_t;
void solar_pd_power_init(solar_pd_power_t *, const solar_pd_power_backend_t *, void *, uint16_t, uint16_t);
void solar_pd_power_poll(solar_pd_power_t *, uint32_t);
bool solar_pd_power_request(solar_pd_power_t *, unsigned, uint16_t, uint32_t);
const char *solar_pd_power_state_name(solar_pd_power_state_t);

/* Per-session demo. No bus access, persistent settings or power switching. */
typedef enum { SOLAR_PD_POWER_DEMO_NORMAL, SOLAR_PD_POWER_DEMO_REJECT,
               SOLAR_PD_POWER_DEMO_TIMEOUT, SOLAR_PD_POWER_DEMO_DISCONNECT,
               SOLAR_PD_POWER_DEMO_IO_ERROR, SOLAR_PD_POWER_DEMO_MISMATCH } solar_pd_power_demo_mode_t;
typedef struct {
    solar_pd_power_snapshot_t source;
    solar_pd_power_profile_t request;
    solar_pd_power_demo_mode_t mode;
    uint32_t began;
    bool pending;
} solar_pd_power_demo_t;
void solar_pd_power_demo_init(solar_pd_power_demo_t *, solar_pd_power_demo_mode_t);
extern const solar_pd_power_backend_t solar_pd_power_demo_backend;
