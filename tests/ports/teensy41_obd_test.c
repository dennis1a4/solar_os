#include "solar_os_obd_demo.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool queue_emit(void *q, const solar_can_frame_t *f) { return solar_can_push(q, f); }
static void pump(solar_obd_t *o, solar_obd_demo_t *d, uint32_t *now) {
    unsigned limit = 20000;
    while (o->active && limit--) {
        *now += 10;
        solar_obd_demo_poll(d, *now);
        solar_obd_poll(o, *now);
    }
    assert(limit && o->done);
}
static void scan_demo(solar_obd_demo_mode_t mode) {
    solar_can_t bus;
    solar_can_init(&bus);
    bus.enabled[0] = true;
    bus.listen_only[0] = false;
    solar_obd_t o;
    solar_obd_demo_t d;
    solar_obd_init(&o, &bus, 0);
    solar_obd_demo_init(&d, &bus, mode);
    uint32_t now = 0xfffff000U; // scan and ISO-TP deadlines cross clock wrap
    assert(solar_obd_scan(&o, now));
    assert(!solar_obd_scan(&o, now));
    pump(&o, &d, &now);
    if (mode == OBD_DEMO_TIMEOUT) {
        assert(!o.ecus[0].seen && !o.ecus[0].valid && o.timeouts == 4);
        assert(!solar_obd_clear(&o, 0, now));
        return;
    }
    assert(o.ecus[0].seen && o.ecus[1].seen && !o.ecus[2].seen);
    assert(o.ecus[0].readiness[0] == 0x84);
    if (mode == OBD_DEMO_SEQUENCE) {
        assert(!(o.ecus[0].valid & 2));
        assert(o.errors > 0);
        assert(o.ecus[1].valid == 15);
        return;
    }
    assert(o.ecus[0].valid == 15 && o.ecus[1].valid == 15 && !o.errors && !o.timeouts);
    assert(o.ecus[0].count[0] == 4 && o.ecus[0].codes[0][3] == 0x0420);
    assert(o.ecus[1].codes[0][0] == 0x0700);
    assert(solar_obd_clear(&o, 0, now));
    pump(&o, &d, &now);
    if (mode == OBD_DEMO_REJECT) {
        assert(o.ecus[0].clear_result == 3 && o.ecus[0].clear_nrc == 0x22);
        assert(o.ecus[0].count[0] == 4);
    } else {
        assert(o.ecus[0].clear_result == 1 && o.ecus[0].count[0] == 0 && o.ecus[0].count[1] == 0);
        assert(o.ecus[0].count[2] == 1 && o.ecus[0].codes[2][0] == 0x0420);
        assert(o.ecus[0].readiness[0] == 0 && o.ecus[0].readiness[3] == 0x65);
        assert(o.ecus[1].count[0] == 1 && !d.cleared[1]);
    }
    FILE *f = tmpfile();
    assert(f);
    assert(solar_obd_report(&o, f, "host-test"));
    rewind(f);
    char report[4096];
    size_t size = fread(report, 1, sizeof(report) - 1, f);
    report[size] = 0;
    assert(strstr(report, "DEMO") && strstr(report, "ECU 7E8") && strstr(report, "P0420"));
    fclose(f);
    assert(solar_obd_clear(&o, 1, now));
    d.mode = OBD_DEMO_TIMEOUT;
    pump(&o, &d, &now);
    assert(o.ecus[1].clear_result == 2 && !o.ecus[1].valid);
}
static void queues(void) {
    solar_can_t b;
    solar_can_init(&b);
    solar_can_frame_t f = {.id = 0x7ff, .length = 8}, out;
    assert(!solar_can_send(&b, &f));
    b.enabled[0] = true;
    assert(!solar_can_send(&b, &f));
    b.listen_only[0] = false;
    for (unsigned i = 0; i < SOLAR_CAN_QUEUE; ++i) {
        f.timestamp_ms = i;
        assert(solar_can_send(&b, &f));
    }
    assert(!solar_can_send(&b, &f) && b.tx.dropped == 1);
    for (unsigned i = 0; i < SOLAR_CAN_QUEUE; ++i) {
        assert(solar_can_pop(&b.tx, &out));
        assert(out.timestamp_ms == i);
    }
    assert(!solar_can_pop(&b.tx, &out));
    f.id = 0x800;
    assert(!solar_can_valid(&f));
    f.extended = true;
    assert(solar_can_valid(&f));
    f.id = 0x20000000;
    assert(!solar_can_valid(&f));
    f.id = 1;
    f.length = 9;
    assert(!solar_can_valid(&f));
    f.length = 8;
    f.channel = 2;
    assert(!solar_can_valid(&f));
}
static void transport(void) {
    solar_can_queue_t q = {0};
    solar_isotp_t rx, tx;
    solar_isotp_init(&rx, 0x708, 0x700, 0, false, queue_emit, &q);
    solar_isotp_init(&tx, 0x700, 0x708, 0, false, queue_emit, &q);
    uint8_t data[SOLAR_ISOTP_MAX];
    for (unsigned i = 0; i < sizeof(data); ++i)
        data[i] = i;
    assert(solar_isotp_send(&tx, data, sizeof(data), 0));
    assert(!solar_isotp_send(&tx, data, 1, 0));
    for (uint32_t now = 0; now < 100 && !rx.complete; ++now) {
        solar_can_frame_t f;
        while (solar_can_pop(&q, &f)) {
            if (f.id == 0x700)
                solar_isotp_receive(&tx, &f, now);
            else
                solar_isotp_receive(&rx, &f, now);
        }
        solar_isotp_poll(&tx, now);
        solar_isotp_poll(&rx, now);
    }
    assert(rx.complete && rx.rx_size == sizeof(data) && !memcmp(rx.rx, data, sizeof(data)));
    assert(!tx.transmitting && !tx.errors && !rx.errors);
    // Declared length > bound causes overflow FC, never buffer overwrite.
    solar_can_frame_t f = {.id = 0x708, .length = 8, .data = {0x11, 1, 0, 0, 0, 0, 0, 0}};
    solar_isotp_receive(&rx, &f, 200);
    assert(!rx.complete && !rx.receiving && rx.errors == 1);
    solar_can_frame_t fc;
    assert(solar_can_pop(&q, &fc));
    assert(fc.data[0] == 0x32);
    f.data[0] = 0x10;
    f.data[1] = 10;
    solar_isotp_receive(&rx, &f, 300);
    solar_isotp_poll(&rx, 1300);
    assert(rx.timeouts == 1 && !rx.receiving);
    f.data[0] = 0x21;
    solar_isotp_receive(&rx, &f, 1301);
    assert(!rx.complete);
    // Missing flow control and bounded WAIT handling.
    memset(&q, 0, sizeof(q));
    solar_isotp_init(&tx, 0x700, 0x708, 0, false, queue_emit, &q);
    assert(solar_isotp_send(&tx, data, 20, 0));
    solar_isotp_poll(&tx, 1000);
    assert(!tx.transmitting && tx.timeouts == 1);
    assert(solar_isotp_send(&tx, data, 20, 1001));
    fc = (solar_can_frame_t){.id = 0x700, .length = 3, .data = {0x31, 0, 0}};
    for (unsigned i = 0; i < 4; ++i)
        solar_isotp_receive(&tx, &fc, 1100 + i);
    assert(!tx.transmitting && tx.errors == 1);
    // Respect block size and separation, including conservative microsecond STmin.
    memset(&q, 0, sizeof(q));
    assert(solar_isotp_send(&tx, data, 30, 2000));
    assert(solar_can_pop(&q, &f));
    fc.data[0] = 0x30;
    fc.data[1] = 1;
    fc.data[2] = 0xf1;
    solar_isotp_receive(&tx, &fc, 2000);
    solar_isotp_poll(&tx, 2000);
    assert(!q.count);
    solar_isotp_poll(&tx, 2001);
    assert(q.count == 1 && tx.waiting_fc);
    solar_isotp_poll(&tx, 2002);
    assert(q.count == 1);
    fc.data[0] = 0x32;
    solar_isotp_receive(&tx, &fc, 2003);
    assert(!tx.transmitting);
    // Wrong channel, extended flag and remote requests do not enter normal-addressed RX.
    solar_isotp_init(&rx, 0x708, 0x700, 0, false, queue_emit, &q);
    f = (solar_can_frame_t){.id = 0x708, .channel = 1, .length = 2, .data = {1, 0x44}};
    solar_isotp_receive(&rx, &f, 0);
    assert(!rx.complete);
    f.channel = 0;
    f.extended = true;
    solar_isotp_receive(&rx, &f, 0);
    assert(!rx.complete);
    f.extended = false;
    f.remote = true;
    solar_isotp_receive(&rx, &f, 0);
    assert(!rx.complete);
    f.remote = false;
    f.data[0] = 7;
    solar_isotp_receive(&rx, &f, 0);
    assert(!rx.complete && rx.errors == 1);
}
static void decode(void) {
    char code[6];
    solar_obd_dtc(0xd016, code);
    assert(!strcmp(code, "U1016"));
    solar_obd_dtc(0x4133, code);
    assert(!strcmp(code, "C0133"));
    solar_obd_dtc(0x8133, code);
    assert(!strcmp(code, "B0133"));
    solar_obd_ecu_t e = {0};
    uint8_t p[] = {0x43, 2, 0x03, 0x00, 0x04, 0x20};
    assert(solar_obd_decode(&e, 1, p, sizeof(p)) && e.count[0] == 2);
    assert(!solar_obd_decode(&e, 1, p, 5));
    assert(e.count[0] == 2);
    p[1] = 33;
    assert(!solar_obd_decode(&e, 1, p, sizeof(p)));
    p[1] = 2;
    p[4] = p[5] = 0;
    assert(!solar_obd_decode(&e, 1, p, sizeof(p)));
    uint8_t nrc[] = {0x7f, 3, 0x11};
    assert(solar_obd_decode(&e, 1, nrc, 3));
    assert(e.negative[1] == 0x11);
    assert(!solar_obd_decode(&e, 2, nrc, 3));
}
int main(void) {
    queues();
    transport();
    decode();
    for (unsigned i = 0; i < 4; ++i)
        scan_demo(i);
    puts("PASS: CAN bounds/listen-only; ISO-TP flow control, wrap, malformed/timeout; OBD "
         "multi-ECU scan, clear, rejection, report");
}
