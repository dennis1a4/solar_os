#include "solar_os_mqtt_capture.h"
#include <string.h>

const solar_os_mqtt_capture_message_t *
solar_os_mqtt_capture_find(const solar_os_mqtt_capture_model_t *m, uint64_t seq) {
    if (!seq || seq > m->received || m->received - seq >= MQTT_CAPTURE_HISTORY)
        return NULL;
    const solar_os_mqtt_capture_message_t *entry = &m->history[(seq - 1) % MQTT_CAPTURE_HISTORY];
    return entry->sequence == seq ? entry : NULL;
}
void solar_os_mqtt_capture_record(solar_os_mqtt_capture_model_t *m,
                                  const solar_os_mqtt_capture_message_t *msg) {
    solar_os_mqtt_capture_message_t *entry = &m->history[m->received % MQTT_CAPTURE_HISTORY];
    *entry = *msg;
    entry->sequence = ++m->received;
    if (m->received > MQTT_CAPTURE_HISTORY)
        ++m->evicted;
    if (entry->truncated)
        ++m->truncated;
    if (entry->topic_size >= MQTT_CAPTURE_TOPIC_BYTES) {
        ++m->unindexed;
        return;
    }
    int parent = -1;
    for (size_t end = 0;; ++end) {
        if (entry->topic[end] && entry->topic[end] != '/')
            continue;
        int node = -1;
        for (unsigned i = 0; i < m->topic_count; ++i)
            if (m->topics[i].parent == parent && strlen(m->topics[i].path) == end &&
                !memcmp(m->topics[i].path, entry->topic, end)) {
                node = (int)i;
                break;
            }
        if (node < 0) {
            if (m->topic_count == MQTT_CAPTURE_TOPICS) {
                ++m->unindexed;
                return;
            }
            node = m->topic_count++;
            memcpy(m->topics[node].path, entry->topic, end);
            m->topics[node].path[end] = 0;
            m->topics[node].parent = parent;
        }
        parent = node;
        if (!entry->topic[end]) {
            ++m->topics[node].count;
            m->topics[node].latest = *entry;
            break;
        }
    }
}
