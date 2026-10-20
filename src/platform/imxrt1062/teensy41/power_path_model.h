#pragma once
#include "power_path.h"
// Caller serializes operations. No RTOS, heap, GPIO or bus ownership in this driver.
struct SkPowerPathIo {
    void *user;
    bool (*read)(void *, uint8_t address, uint8_t reg, uint8_t *, size_t);
    bool (*write)(void *, uint8_t address, uint8_t reg, const uint8_t *, size_t);
};
class SkPowerPathDriver {
public:
    explicit SkPowerPathDriver(SkPowerPathIo transport):io(transport) {}
    SkPowerPathSnapshot state{};
    bool poll_gpio();
    bool poll_meter();
    bool set_limit(uint16_t ma);
    bool usb(const char *operation);
    bool cut();
private:
    SkPowerPathIo io;
    bool expander_initialized=false, meter_initialized=false;
    bool read8(uint8_t reg,uint8_t &value);
    bool write8(uint8_t reg,uint8_t value);
    bool read16(uint8_t reg,uint16_t &value);
    bool write16(uint8_t reg,uint16_t value);
    bool configure_expander();
    bool configure_meter();
    bool apply_limit();
};
// Does not consume clear-on-read alerts, RX messages or source capabilities.
bool sk_power_pd_observe(const SkPowerPathIo &, SkPowerPdSnapshot *);
void sk_power_path_format(const SkPowerPathSnapshot &, const SkPowerPdSnapshot &, char lines[9][27]);
