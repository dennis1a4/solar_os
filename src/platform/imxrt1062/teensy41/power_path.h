#pragma once
#include <stddef.h>
#include <stdint.h>
#ifndef SK_POWER_PATH
#define SK_POWER_PATH 0
#endif
#ifndef SK_POWER_MCP_ADDRESS
#define SK_POWER_MCP_ADDRESS 0x20
#endif
#ifndef SK_POWER_INA_ADDRESS
#define SK_POWER_INA_ADDRESS 0x40
#endif
#ifndef SK_POWER_PD_ADDRESS
#define SK_POWER_PD_ADDRESS 0x28
#endif
#ifndef SK_POWER_MAX_CURRENT_MA
#define SK_POWER_MAX_CURRENT_MA 5000
#endif
struct SkPowerPathSnapshot {
    bool expander_connected=false, meter_connected=false, meter_valid=false;
    bool usb_enabled=false, usb_fault=false, usb_latched=false, usb_attached=false;
    bool kill_low=false, button_low=false, control_error=false;
    bool limit_verified=false;
    uint8_t valid_inputs=0;
    uint16_t limit_ma=0;
    uint32_t mv=0, mw=0;
    int32_t ma=0;
};
struct SkPowerPdSnapshot {
    bool connected=false, attached=false, ready=false, rdo_valid=false;
    uint16_t set_mv=0, rdo_ma=0;
};
#if SK_POWER_PATH
void sk_power_path_begin();
void sk_power_path_snapshot(SkPowerPathSnapshot *);
bool sk_power_path_usb(const char *operation);
bool sk_power_path_usb_off();
bool sk_power_path_cut();
void sk_power_path_lines(char lines[9][27]);
// Reads only non-destructive status registers unless the explicit pd service owns
// the controller, in which case it copies that service's snapshot.
void sk_pd_board_snapshot(SkPowerPdSnapshot *);
#else
inline void sk_power_path_begin() {}
inline bool sk_power_path_cut() { return true; }
#endif
