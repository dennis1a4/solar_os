#include "solar_os_mqtt_capture.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "solar_os_memory.h"
#include "solar_os_mqtt_wire.h"
#include "solar_os_net_session.h"
#include "solar_os_task.h"
#include <stdio.h>
#include <string.h>

struct solar_os_mqtt_capture {
    solar_os_mqtt_capture_model_t model;
    solar_os_mqtt_capture_config_t config;
    solar_os_mqtt_capture_status_t status;
    SemaphoreHandle_t mutex;
    TaskHandle_t task;
    volatile bool stop, done;
    solar_os_net_session_t *net;
    uint32_t channel, last_tx, ping_at, phase_at;
    unsigned phase;
    bool ping;
    solar_os_mqtt_wire_t wire;
    uint8_t rx[1024], tx[400];
};
static uint32_t now_ms(void) { return xTaskGetTickCount() * portTICK_PERIOD_MS; }
static bool cancelled(void *user) { return ((solar_os_mqtt_capture_t *)user)->stop; }
solar_os_mqtt_capture_model_t *solar_os_mqtt_capture_lock(solar_os_mqtt_capture_t *c,
                                                          solar_os_mqtt_capture_status_t *s) {
    xSemaphoreTake(c->mutex, portMAX_DELAY);
    if (s)
        *s = c->status;
    return &c->model;
}
void solar_os_mqtt_capture_unlock(solar_os_mqtt_capture_t *c) { xSemaphoreGive(c->mutex); }
static void status(solar_os_mqtt_capture_t *c, const char *message, bool connected) {
    solar_os_mqtt_capture_lock(c, NULL);
    if (c->status.connected && !connected)
        ++c->status.interruptions;
    if (!c->status.connected && connected)
        ++c->status.connections;
    c->status.connected = connected;
    snprintf(c->status.detail, sizeof(c->status.detail), "%s", message);
    solar_os_mqtt_capture_unlock(c);
}
static bool send(solar_os_mqtt_capture_t *c, const uint8_t *bytes, size_t size) {
    if (!size || solar_os_net_session_tcp_send(c->net, c->channel, bytes, size, 2000) != ESP_OK)
        return false;
    c->last_tx = now_ms();
    return true;
}
static bool packet(void *user, uint8_t header, uint16_t id, const uint8_t *body, size_t size,
                   const solar_os_mqtt_capture_message_t *message) {
    solar_os_mqtt_capture_t *c = user;
    switch (header >> 4) {
    case 2:
        if (c->phase != 0 || size != 2 || body[0] != 0 || body[1] != 0) {
            status(c, "Broker refused connection or invalid CONNACK", false);
            return false;
        }
        c->phase = 1;
        c->phase_at = now_ms();
        return send(c, c->tx, solar_os_mqtt_wire_subscribe(c->tx, sizeof(c->tx), 1));
    case 9:
        if (c->phase != 1 || size != 4 || body[0] != 0 || body[1] != 1 ||
            (body[2] > 1 && body[2] != 128) || (body[3] > 1 && body[3] != 128) ||
            (body[2] == 128 && body[3] == 128)) {
            status(c, "Subscription rejected (# or $SYS/#)", false);
            return false;
        }
        c->phase = 2;
        status(c, body[2] == 128 ? "# denied; subscribed to $SYS/#" :
               body[3] == 128 ? "$SYS/# denied; subscribed to #" : "Subscribed to # and $SYS/#", true);
        return true;
    case 13:
        if (!c->ping)
            return false;
        c->ping = false;
        return true;
    case 3:
        if (!c->phase)
            return false;
        c->wire.message.received_ms = now_ms();
        solar_os_mqtt_capture_record(solar_os_mqtt_capture_lock(c, NULL), message);
        solar_os_mqtt_capture_unlock(c);
        if (message->qos) {
            uint8_t ack[] = {0x40, 2, id >> 8, id};
            return send(c, ack, sizeof(ack));
        }
        return true;
    default:
        return false;
    }
}
static void worker(void *user) {
    solar_os_mqtt_capture_t *c = user;
    if (solar_os_net_session_create("mqtt-explorer", cancelled, c, &c->net) != ESP_OK) {
        status(c, "Cannot allocate network session", false);
        goto done;
    }
    while (!c->stop) {
        status(c, "Connecting...", false);
        esp_err_t err = solar_os_net_session_tcp_connect(c->net, c->config.host, c->config.port,
                                                         5000, &c->channel);
        if (err == ESP_OK) {
            solar_os_mqtt_wire_init(&c->wire);
            c->phase = 0;
            c->phase_at = now_ms();
            c->ping = false;
            uint32_t nonce[2];
            esp_fill_random(nonce, sizeof(nonce));
            char client[32];
            snprintf(client, sizeof(client), "solaros-%08lx-%08lx", (unsigned long)nonce[0],
                     (unsigned long)nonce[1]);
            bool ok = send(c, c->tx,
                           solar_os_mqtt_wire_connect(c->tx, sizeof(c->tx), client,
                                                      c->config.username, c->config.password));
            if (ok)
                status(c, "Awaiting broker acknowledgement", false);
            while (ok && !c->stop) {
                solar_os_net_receive_result_t result;
                err = solar_os_net_session_tcp_receive(c->net, c->channel, c->rx, sizeof(c->rx), 50,
                                                       &result);
                if (err != ESP_OK || result.closed) {
                    status(c, "Connection lost; retrying", false);
                    break;
                }
                if (result.data_len &&
                    !solar_os_mqtt_wire_feed(&c->wire, c->rx, result.data_len, packet, c)) {
                    solar_os_mqtt_capture_lock(c, NULL);
                    ++c->status.protocol_errors;
                    solar_os_mqtt_capture_unlock(c);
                    if (c->phase == 2)
                        status(c, "Invalid/oversized MQTT packet; retrying", false);
                    break;
                }
                uint32_t now = now_ms();
                if ((c->phase < 2 && now - c->phase_at > 10000) ||
                    (c->ping && now - c->ping_at > 10000)) {
                    status(c, "Broker response timeout; retrying", false);
                    break;
                }
                if (c->phase == 2 && !c->ping && now - c->last_tx >= 15000) {
                    static const uint8_t ping[] = {0xc0, 0};
                    ok = send(c, ping, sizeof(ping));
                    c->ping = true;
                    c->ping_at = now;
                }
            }
            solar_os_net_session_close(c->net, c->channel);
            c->channel = 0;
        } else
            status(c, "TCP connection failed; retrying", false);
        // Also account for a failed send, without replacing the useful rejection detail.
        solar_os_mqtt_capture_lock(c, NULL);
        if (c->status.connected) {
            c->status.connected = false;
            ++c->status.interruptions;
            snprintf(c->status.detail, sizeof(c->status.detail), "Connection lost; retrying");
        }
        solar_os_mqtt_capture_unlock(c);
        uint32_t start = now_ms();
        while (!c->stop && now_ms() - start < 2000)
            vTaskDelay(pdMS_TO_TICKS(20));
    }
    solar_os_net_session_destroy(c->net);
    c->net = NULL;
done:
    c->done = true;
    solar_os_task_delete_internal(NULL);
}
esp_err_t solar_os_mqtt_capture_start(const solar_os_mqtt_capture_config_t *config,
                                      solar_os_mqtt_capture_t **out) {
    if (!out)
        return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!config || !config->host[0] || !config->port ||
        !memchr(config->host, 0, sizeof(config->host)) ||
        !memchr(config->username, 0, sizeof(config->username)) ||
        !memchr(config->password, 0, sizeof(config->password)) ||
        (config->password[0] && !config->username[0]))
        return ESP_ERR_INVALID_ARG;
    solar_os_mqtt_capture_t *c =
        solar_os_memory_calloc(1, sizeof(*c), SOLAR_OS_MEMORY_EXTERNAL_REQUIRED, "mqtt.capture");
    if (!c)
        return ESP_ERR_NO_MEM;
    c->config = *config;
    c->mutex = xSemaphoreCreateMutex();
    if (!c->mutex) {
        solar_os_memory_free(c);
        return ESP_ERR_NO_MEM;
    }
    snprintf(c->status.detail, sizeof(c->status.detail), "Starting MQTT capture");
    if (solar_os_task_create_pinned_internal(worker, "mqtt-capture", 8192, c, 1, &c->task,
                                             tskNO_AFFINITY,
                                             SOLAR_OS_TASK_ROLE_FOREGROUND) != pdPASS) {
        vSemaphoreDelete(c->mutex);
        memset(c, 0, sizeof(*c));
        solar_os_memory_free(c);
        return ESP_ERR_NO_MEM;
    }
    *out = c;
    return ESP_OK;
}
void solar_os_mqtt_capture_halt(solar_os_mqtt_capture_t *c) {
    if (!c)
        return;
    c->stop = true;
    while (!solar_os_task_wait_done(c->task, &c->done, SOLAR_OS_TASK_STOP_WAIT_MS))
        vTaskDelay(1);
    c->task = NULL;
}
void solar_os_mqtt_capture_stop(solar_os_mqtt_capture_t *c) {
    if (!c)
        return;
    solar_os_mqtt_capture_halt(c);
    vSemaphoreDelete(c->mutex);
    memset(&c->config, 0, sizeof(c->config));
    solar_os_memory_free(c);
}
