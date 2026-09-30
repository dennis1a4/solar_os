#include "solar_os_can.h"
#include <string.h>
bool solar_can_valid(const solar_can_frame_t *f) {
    return f && f->channel < 2 && f->length <= 8 && f->id <= (f->extended ? 0x1fffffffU : 0x7ffU);
}
bool solar_can_push(solar_can_queue_t *q, const solar_can_frame_t *f) {
    if (!q || !solar_can_valid(f))
        return false;
    if (q->count == SOLAR_CAN_QUEUE) {
        ++q->dropped;
        return false;
    }
    q->frames[(q->head + q->count++) % SOLAR_CAN_QUEUE] = *f;
    return true;
}
bool solar_can_pop(solar_can_queue_t *q, solar_can_frame_t *f) {
    if (!q || !f || !q->count)
        return false;
    *f = q->frames[q->head];
    q->head = (q->head + 1) % SOLAR_CAN_QUEUE;
    --q->count;
    return true;
}
void solar_can_init(solar_can_t *bus) {
    memset(bus, 0, sizeof(*bus));
    bus->listen_only[0] = bus->listen_only[1] = true;
}
bool solar_can_send(solar_can_t *bus, const solar_can_frame_t *f) {
    if (!solar_can_valid(f) || !bus->enabled[f->channel] || bus->listen_only[f->channel]) {
        ++bus->rejected;
        return false;
    }
    return solar_can_push(&bus->tx, f);
}
