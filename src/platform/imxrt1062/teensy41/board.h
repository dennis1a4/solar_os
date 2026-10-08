#pragma once
#include <stdint.h>

// Override in the selected PlatformIO profile for temporary display wiring.
#ifndef SK_PRIMARY_CS
#define SK_PRIMARY_CS 9
#endif
#ifndef SK_PRIMARY_RESET
#define SK_PRIMARY_RESET 14
#endif
#ifndef SK_PRIMARY_WAIT
#define SK_PRIMARY_WAIT 15
#endif
#ifndef SK_PRIMARY_BACKLIGHT
#define SK_PRIMARY_BACKLIGHT -1
#endif
#ifndef SK_MOTOR_ENABLE_PIN
#define SK_MOTOR_ENABLE_PIN 10
#endif
#ifndef SK_SECONDARY_SPI_BUS
#define SK_SECONDARY_SPI_BUS 1
#endif

#ifndef SK_AMPLIFIER_SHUTDOWN_PIN
#define SK_AMPLIFIER_SHUTDOWN_PIN 40
#endif
#ifndef SK_UART_CONSOLE
#define SK_UART_CONSOLE 1
#endif
static_assert(!SK_UART_CONSOLE || SK_AMPLIFIER_SHUTDOWN_PIN != 0, "AmpEn pin 0 conflicts with Serial1 RX");
namespace superkeyboard {
constexpr uint8_t console_rx = 0, console_tx = 1;
constexpr uint8_t shift_clock = 2, nes_latch = 3, nes_data = 4;
constexpr uint8_t shift_latch = 5, shift_data = 6;
constexpr uint8_t audio_out = 7, audio_in = 8;
constexpr uint8_t primary_cs = SK_PRIMARY_CS, primary_reset = SK_PRIMARY_RESET;
constexpr int primary_wait = SK_PRIMARY_WAIT, primary_backlight = SK_PRIMARY_BACKLIGHT;
constexpr int motor_enable = SK_MOTOR_ENABLE_PIN;
constexpr uint8_t relay = 22;
static_assert(motor_enable < 0 || motor_enable != primary_cs, "Display CS conflicts with motor enable");
static_assert(primary_backlight < 0 || (primary_backlight != primary_cs &&
    primary_backlight != primary_reset && primary_backlight != primary_wait), "Display backlight pin conflict");
constexpr uint8_t primary_mosi = 11, primary_miso = 12, primary_sck = 13;
constexpr uint8_t wire1_scl = 16, wire1_sda = 17;
constexpr uint8_t audio_sda = 18, audio_scl = 19;
constexpr uint8_t audio_lrclk = 20, audio_bclk = 21, audio_mclk = 23;
constexpr uint8_t wire2_scl = 24, wire2_sda = 25;
constexpr uint8_t shared_mosi = 26, shared_sck = 27, shared_miso = 39;
constexpr uint8_t secondary_reset = 30, secondary_dc = 31;
constexpr uint8_t secondary_cs = 32, secondary_backlight = 33;
static_assert(SK_SECONDARY_SPI_BUS == 0 || SK_SECONDARY_SPI_BUS == 1, "Unsupported secondary SPI bus");
constexpr uint8_t secondary_spi = SK_SECONDARY_SPI_BUS;
constexpr uint8_t secondary_mosi = secondary_spi == 0 ? primary_mosi : shared_mosi;
constexpr uint8_t secondary_sck = secondary_spi == 0 ? primary_sck : shared_sck;
constexpr uint8_t vin_sense = 38, amplifier_shutdown = SK_AMPLIFIER_SHUTDOWN_PIN, volume = 41;
struct SlotPins {
    uint8_t spi, cs, i2c, uart, rx, tx;
};
constexpr SlotPins slots[] = {
    {1, 37, 2, 7, 28, 29},
    {1, 36, 1, 8, 34, 35},
    {0, 9, 2, 3, 15, 14},
};
}
