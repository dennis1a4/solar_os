#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define SOLAR_OS_MAG_NAME_MAX 20
#define SOLAR_OS_MAG_DRIVER_MAX 24

/* mag.<device> is a shared byte source. Reads return one 24-byte mag-v1-le
 * record: uint64 timestamp_us, uint32 valid, float32 magnetic_field_ut[3].
 * All values use little endian encoding without padding. Field units are uT. */
#define SOLAR_OS_MAG_STREAM_FORMAT "mag-v1-le"
#define SOLAR_OS_MAG_STREAM_SAMPLE_BYTES 24U

typedef uint32_t solar_os_mag_capabilities_t;

typedef enum {
    SOLAR_OS_MAG_CAP_FIELD = 1U << 0,
} solar_os_mag_capability_t;

typedef struct {
    uint64_t timestamp_us;
    solar_os_mag_capabilities_t valid;
    float magnetic_field_ut[3];
} solar_os_mag_sample_t;

typedef struct {
    esp_err_t (*read_sample)(void *ctx,
                             uint32_t timeout_ms,
                             solar_os_mag_sample_t *sample);
} solar_os_mag_ops_t;

typedef struct {
    const char *name;
    const char *driver;
    solar_os_mag_capabilities_t capabilities;
    const solar_os_mag_ops_t *ops;
    void *ctx;
} solar_os_mag_registration_t;

typedef struct {
    char name[SOLAR_OS_MAG_NAME_MAX];
    char driver[SOLAR_OS_MAG_DRIVER_MAX];
    solar_os_mag_capabilities_t capabilities;
} solar_os_mag_info_t;

esp_err_t solar_os_mag_register(const solar_os_mag_registration_t *registration);
esp_err_t solar_os_mag_unregister(const char *name);
size_t solar_os_mag_count(void);
bool solar_os_mag_get(size_t index, solar_os_mag_info_t *info);
esp_err_t solar_os_mag_read_sample(const char *name,
                                   uint32_t timeout_ms,
                                   solar_os_mag_sample_t *sample);
