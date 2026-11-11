#include "solar_os_mag.h"
#include "solar_os_sync.h"
#include "solar_os_stream.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MAG_DEVICE_MAX 3U
#define MAG_CAP_ALL SOLAR_OS_MAG_CAP_FIELD

typedef struct {
    bool active;
    solar_os_mag_info_t info;
    solar_os_mag_ops_t ops;
    void *ctx;
    size_t refs;
    uint32_t generation;
} mag_device_t;

static SemaphoreHandle_t mag_mutex;
static EXT_RAM_BSS_ATTR StaticSemaphore_t mag_mutex_storage;
static EXT_RAM_BSS_ATTR mag_device_t mag_devices[MAG_DEVICE_MAX];
static uint32_t mag_next_generation = 1U;

static esp_err_t ensure_mutex(void)
{
    (void)solar_os_mutex_get(&mag_mutex, &mag_mutex_storage);
    return mag_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static bool name_valid(const char *name)
{
    return name != NULL && name[0] != '\0' &&
        strnlen(name, SOLAR_OS_MAG_NAME_MAX) < SOLAR_OS_MAG_NAME_MAX;
}

static void pack_u32(uint8_t *out, uint32_t value)
{ for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (8*i)); }

static void pack_floats(uint8_t *out, const float *in, unsigned count)
{
    _Static_assert(sizeof(float) == 4, "MAG streams require float32");
    for (unsigned i = 0; i < count; ++i) {
        uint32_t bits; memcpy(&bits, in+i, sizeof(bits)); pack_u32(out+4*i, bits);
    }
}

static esp_err_t stream_read(void *user, solar_os_stream_handle_t *handle,
    void *data, size_t len, uint32_t timeout_ms, size_t *read_len)
{
    (void)handle;
    *read_len = 0;
    if (len < SOLAR_OS_MAG_STREAM_SAMPLE_BYTES) return ESP_ERR_INVALID_SIZE;
    const mag_device_t *device = user;
    solar_os_mag_sample_t sample;
    esp_err_t e = solar_os_mag_read_sample(device->info.name, timeout_ms, &sample);
    if (e != ESP_OK) return e;
    uint8_t *out = data;
    for (unsigned i = 0; i < 8; ++i) out[i] = (uint8_t)(sample.timestamp_us >> (8*i));
    pack_u32(out+8, sample.valid);
    pack_floats(out+12, sample.magnetic_field_ut, 3);
    *read_len = SOLAR_OS_MAG_STREAM_SAMPLE_BYTES;
    return ESP_OK;
}

static esp_err_t stream_header(void *user, char *out, size_t len)
{
    (void)user;
    const char *header = "sample_us,valid,mx_uT,my_uT,mz_uT";
    return strlcpy(out, header, len) < len ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t stream_csv(void *user, solar_os_stream_handle_t *handle,
    const solar_os_stream_read_options_t *options, solar_os_stream_csv_record_t *record)
{
    (void)handle;
    const mag_device_t *device = user;
    solar_os_mag_sample_t s;
    esp_err_t e = solar_os_mag_read_sample(device->info.name, options ? options->timeout_ms : 1000, &s);
    if (e != ESP_OK) return e;
    memset(record, 0, sizeof(*record));
    record->has_data = true; record->uptime_ms = s.timestamp_us / 1000;
    int n = snprintf(record->line, sizeof(record->line),
        "%" PRIu64 ",%" PRIu32 ",%.6g,%.6g,%.6g",
        s.timestamp_us, s.valid, (double)s.magnetic_field_ut[0],
        (double)s.magnetic_field_ut[1], (double)s.magnetic_field_ut[2]);
    snprintf(record->change_key, sizeof(record->change_key), "%" PRIu64, s.timestamp_us);
    return n >= 0 && (size_t)n < sizeof(record->line) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t solar_os_mag_register(const solar_os_mag_registration_t *registration)
{
    if (registration == NULL || !name_valid(registration->name) ||
        registration->driver == NULL || registration->ops == NULL ||
        registration->ops->read_sample == NULL ||
        registration->capabilities == 0U ||
        (registration->capabilities & ~MAG_CAP_ALL) != 0U ||
        strnlen(registration->driver, SOLAR_OS_MAG_DRIVER_MAX) >=
            SOLAR_OS_MAG_DRIVER_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t mutex_ret = ensure_mutex();
    if (mutex_ret != ESP_OK) {
        return mutex_ret;
    }

    xSemaphoreTake(mag_mutex, portMAX_DELAY);
    mag_device_t *free_device = NULL;
    for (size_t i = 0; i < MAG_DEVICE_MAX; i++) {
        if (mag_devices[i].active &&
            strcmp(mag_devices[i].info.name, registration->name) == 0) {
            xSemaphoreGive(mag_mutex);
            return ESP_ERR_INVALID_STATE;
        }
        if (!mag_devices[i].active && free_device == NULL) {
            free_device = &mag_devices[i];
        }
    }
    if (free_device == NULL) {
        xSemaphoreGive(mag_mutex);
        return ESP_ERR_NO_MEM;
    }

    memset(free_device, 0, sizeof(*free_device));
    free_device->active = true;
    strlcpy(free_device->info.name,
            registration->name,
            sizeof(free_device->info.name));
    strlcpy(free_device->info.driver,
            registration->driver,
            sizeof(free_device->info.driver));
    free_device->info.capabilities = registration->capabilities;
    free_device->ops = *registration->ops;
    free_device->ctx = registration->ctx;
    free_device->generation = mag_next_generation++;
    if (free_device->generation == 0U) {
        free_device->generation = mag_next_generation++;
    }
    solar_os_stream_driver_t stream = {
        .info = {.provider="mag", .type=SOLAR_OS_STREAM_TYPE_BYTES,
            .direction=SOLAR_OS_STREAM_DIRECTION_SOURCE, .sharing=SOLAR_OS_STREAM_SHARING_SHARED,
            .unit="uT", .format=SOLAR_OS_MAG_STREAM_FORMAT, .summary="Timestamped magnetic field in microtesla"},
        .read=stream_read, .csv_header=stream_header, .read_csv=stream_csv, .user=free_device,
    };
    snprintf(stream.info.id, sizeof(stream.info.id), "mag.%s", registration->name);
    strlcpy(stream.info.device, registration->name, sizeof(stream.info.device));
    esp_err_t ret = solar_os_stream_register(&stream);
    if (ret != ESP_OK) memset(free_device, 0, sizeof(*free_device));
    xSemaphoreGive(mag_mutex);
    return ret;
}

esp_err_t solar_os_mag_unregister(const char *name)
{
    if (!name_valid(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t mutex_ret = ensure_mutex();
    if (mutex_ret != ESP_OK) {
        return mutex_ret;
    }

    xSemaphoreTake(mag_mutex, portMAX_DELAY);
    for (size_t i = 0; i < MAG_DEVICE_MAX; i++) {
        if (mag_devices[i].active &&
            strcmp(mag_devices[i].info.name, name) == 0) {
            if (mag_devices[i].refs > 0U) {
                xSemaphoreGive(mag_mutex);
                return ESP_ERR_INVALID_STATE;
            }
            char stream[SOLAR_OS_STREAM_ID_MAX];
            snprintf(stream, sizeof(stream), "mag.%s", name);
            esp_err_t ret = solar_os_stream_unregister(stream);
            if (ret != ESP_OK) { xSemaphoreGive(mag_mutex); return ret; }
            memset(&mag_devices[i], 0, sizeof(mag_devices[i]));
            xSemaphoreGive(mag_mutex);
            return ESP_OK;
        }
    }
    xSemaphoreGive(mag_mutex);
    return ESP_ERR_NOT_FOUND;
}

size_t solar_os_mag_count(void)
{
    if (ensure_mutex() != ESP_OK) {
        return 0U;
    }
    size_t count = 0U;
    xSemaphoreTake(mag_mutex, portMAX_DELAY);
    for (size_t i = 0; i < MAG_DEVICE_MAX; i++) {
        count += mag_devices[i].active ? 1U : 0U;
    }
    xSemaphoreGive(mag_mutex);
    return count;
}

bool solar_os_mag_get(size_t index, solar_os_mag_info_t *info)
{
    if (info == NULL || ensure_mutex() != ESP_OK) {
        return false;
    }
    size_t current = 0U;
    xSemaphoreTake(mag_mutex, portMAX_DELAY);
    for (size_t i = 0; i < MAG_DEVICE_MAX; i++) {
        if (!mag_devices[i].active) {
            continue;
        }
        if (current++ == index) {
            *info = mag_devices[i].info;
            xSemaphoreGive(mag_mutex);
            return true;
        }
    }
    xSemaphoreGive(mag_mutex);
    return false;
}

esp_err_t solar_os_mag_read_sample(const char *name,
                                   uint32_t timeout_ms,
                                   solar_os_mag_sample_t *sample)
{
    if (!name_valid(name) || sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t mutex_ret = ensure_mutex();
    if (mutex_ret != ESP_OK) {
        return mutex_ret;
    }

    solar_os_mag_ops_t ops = {0};
    void *ctx = NULL;
    size_t index = 0U;
    uint32_t generation = 0U;
    solar_os_mag_capabilities_t capabilities = 0U;
    xSemaphoreTake(mag_mutex, portMAX_DELAY);
    for (size_t i = 0; i < MAG_DEVICE_MAX; i++) {
        if (mag_devices[i].active &&
            strcmp(mag_devices[i].info.name, name) == 0) {
            mag_devices[i].refs++;
            ops = mag_devices[i].ops;
            ctx = mag_devices[i].ctx;
            index = i;
            generation = mag_devices[i].generation;
            capabilities = mag_devices[i].info.capabilities;
            break;
        }
    }
    xSemaphoreGive(mag_mutex);
    if (ops.read_sample == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    memset(sample, 0, sizeof(*sample));
    const esp_err_t ret = ops.read_sample(ctx, timeout_ms, sample);
    if (ret == ESP_OK && (sample->valid & ~capabilities) != 0U) {
        sample->valid &= capabilities;
    }

    xSemaphoreTake(mag_mutex, portMAX_DELAY);
    if (mag_devices[index].active &&
        mag_devices[index].generation == generation &&
        mag_devices[index].refs > 0U) {
        mag_devices[index].refs--;
    }
    xSemaphoreGive(mag_mutex);
    return ret;
}
