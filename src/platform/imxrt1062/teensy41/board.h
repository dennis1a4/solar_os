#pragma once
#include <stdint.h>

// Override in the selected PlatformIO profile for temporary display wiring.
#ifndef SK_PRIMARY_CS
#define SK_PRIMARY_CS 9
#endif
#ifndef SK_PRIMARY_RESET
#define SK_PRIMARY_RESET 14
#endif

namespace superkeyboard {
constexpr uint8_t console_rx = 0, console_tx = 1;
constexpr uint8_t shift_clock = 2, nes_latch = 3, nes_data = 4;
constexpr uint8_t shift_latch = 5, shift_data = 6;
constexpr uint8_t audio_out = 7, audio_in = 8;
constexpr uint8_t primary_cs = SK_PRIMARY_CS, primary_reset = SK_PRIMARY_RESET, primary_wait = 15;
constexpr uint8_t motor_enable = 10, relay = 22;
constexpr uint8_t primary_mosi = 11, primary_miso = 12, primary_sck = 13;
constexpr uint8_t wire1_scl = 16, wire1_sda = 17;
constexpr uint8_t audio_sda = 18, audio_scl = 19;
constexpr uint8_t audio_lrclk = 20, audio_bclk = 21, audio_mclk = 23;
constexpr uint8_t wire2_scl = 24, wire2_sda = 25;
constexpr uint8_t shared_mosi = 26, shared_sck = 27, shared_miso = 39;
constexpr uint8_t secondary_reset = 30, secondary_dc = 31;
constexpr uint8_t secondary_cs = 32, secondary_backlight = 33;
constexpr uint8_t vin_sense = 38, amplifier_shutdown = 40, volume = 41;
struct SlotPins {
    uint8_t spi, cs, i2c, uart, rx, tx;
};
constexpr SlotPins slots[] = {
    {1, 37, 2, 7, 28, 29},
    {1, 36, 1, 8, 34, 35},
    {0, 9, 2, 3, 15, 14},
};
}
