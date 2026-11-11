#if SK_MOTION
#include <arduino_freertos.h>
#include "motion_sensors.h"
#include "motion_service.h"
#include "platform.h"
extern "C" {
#include "esp_timer.h"
#include "solar_os_imu.h"
#include "solar_os_mag.h"
}

namespace {
struct Transport {
    bool read(uint8_t address, uint8_t reg, uint8_t *out, size_t length) {
        return sk_i2c_transfer(1, address, &reg, 1, out, length) == ESP_OK;
    }
    bool write(uint8_t address, uint8_t reg, uint8_t value) {
        const uint8_t data[] = {reg, value};
        return sk_i2c_transfer(1, address, data, sizeof(data), nullptr, 0) == ESP_OK;
    }
};
Transport bus;
sk_motion::Sensors<Transport> sensors(bus);
sk_motion::Sample published;
bool imu_available, mag_available;
uint64_t imu_timestamp_us, mag_timestamp_us;
DMAMEM StackType_t worker_stack[768];
StaticTask_t worker_storage;
TaskHandle_t worker_task;

void poll_sensors() {
    const uint32_t now = millis();
    sensors.poll(now);
    const uint64_t timestamp_us = uint64_t(esp_timer_get_time());
    const uint32_t sampled_now = millis();
    taskENTER_CRITICAL();
    if (sensors.sample.imu_valid &&
        (!published.imu_valid || sensors.sample.imu_at != published.imu_at)) {
        imu_timestamp_us = timestamp_us - uint64_t(uint32_t(sampled_now - sensors.sample.imu_at)) * 1000;
    }
    if (sensors.sample.mag_valid &&
        (!published.mag_valid || sensors.sample.mag_at != published.mag_at)) {
        mag_timestamp_us = timestamp_us - uint64_t(uint32_t(sampled_now - sensors.sample.mag_at)) * 1000;
    }
    published = sensors.sample;
    imu_available = sensors.imu_available();
    mag_available = sensors.mag_available();
    taskEXIT_CRITICAL();
}

void run(void *) {
    for (;;) {
        poll_sensors();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t read_cached(bool magnetic, uint32_t timeout_ms, sk_motion::Sample &sample,
                      uint64_t &timestamp_us) {
    const uint64_t deadline = uint64_t(esp_timer_get_time()) + uint64_t(timeout_ms) * 1000;
    for (;;) {
        taskENTER_CRITICAL();
        sample = published;
        const bool available = magnetic ? mag_available : imu_available;
        timestamp_us = magnetic ? mag_timestamp_us : imu_timestamp_us;
        taskEXIT_CRITICAL();
        const uint32_t now = millis();
        if (magnetic ? sample.mag_fresh(now) : sample.imu_fresh(now)) {
            return ESP_OK;
        }
        if (!available) {
            return ESP_ERR_NOT_FOUND;
        }
        if (uint64_t(esp_timer_get_time()) >= deadline) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
}

esp_err_t read_imu(void *, uint32_t timeout_ms, solar_os_imu_sample_t *out) {
    sk_motion::Sample sample;
    uint64_t timestamp_us;
    const esp_err_t result = read_cached(false, timeout_ms, sample, timestamp_us);
    if (result != ESP_OK) return result;
    *out = {};
    out->timestamp_us = timestamp_us;
    out->valid = SOLAR_OS_IMU_CAP_ACCELERATION | SOLAR_OS_IMU_CAP_ANGULAR_VELOCITY;
    const float acceleration[] = {sample.acceleration.x, sample.acceleration.y, sample.acceleration.z};
    const float gyro[] = {sample.gyro.x, sample.gyro.y, sample.gyro.z};
    for (unsigned axis = 0; axis < 3; ++axis) {
        out->acceleration_m_s2[axis] = acceleration[axis] * 9.80665f;
        out->angular_velocity_rad_s[axis] = gyro[axis] / sk_motion::degrees;
    }
    return ESP_OK;
}

esp_err_t read_mag(void *, uint32_t timeout_ms, solar_os_mag_sample_t *out) {
    sk_motion::Sample sample;
    uint64_t timestamp_us;
    const esp_err_t result = read_cached(true, timeout_ms, sample, timestamp_us);
    if (result != ESP_OK) return result;
    *out = {};
    out->timestamp_us = timestamp_us;
    out->valid = SOLAR_OS_MAG_CAP_FIELD;
    out->magnetic_field_ut[0] = sample.magnetic.x;
    out->magnetic_field_ut[1] = sample.magnetic.y;
    out->magnetic_field_ut[2] = sample.magnetic.z;
    return ESP_OK;
}
}

extern "C" esp_err_t sk_motion_begin() {
    if (worker_task) return ESP_OK;
    static const solar_os_imu_ops_t imu_ops = {read_imu};
    static const solar_os_mag_ops_t mag_ops = {read_mag};
    const solar_os_imu_registration_t imu = {
        "imu0", "lsm6dsox", SOLAR_OS_IMU_CAP_ACCELERATION | SOLAR_OS_IMU_CAP_ANGULAR_VELOCITY,
        &imu_ops, nullptr};
    const solar_os_mag_registration_t mag = {
        "mag0", "mmc5983ma", SOLAR_OS_MAG_CAP_FIELD, &mag_ops, nullptr};
    esp_err_t result = solar_os_imu_register(&imu);
    if (result != ESP_OK) return result;
    result = solar_os_mag_register(&mag);
    if (result != ESP_OK) {
        solar_os_imu_unregister("imu0");
        return result;
    }
    // Publish the initial probe before readers can observe the worker startup.
    poll_sensors();
    worker_task = xTaskCreateStatic(run, "motion", 768, nullptr, 1, worker_stack, &worker_storage);
    if (!worker_task) {
        solar_os_mag_unregister("mag0");
        solar_os_imu_unregister("imu0");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
#endif
