#include <arduino_freertos.h>
#include "board.h"
#include "platform.h"

// Enable only after confirming the actual panel and connector wiring.
#ifndef SK_PRIMARY_RA8875
#define SK_PRIMARY_RA8875 0
#endif
#ifndef SK_SECONDARY_ST7735
#define SK_SECONDARY_ST7735 0
#endif
#ifndef SK_USB_HOST
#define SK_USB_HOST 0
#endif
#ifndef SK_AUDIO_SGTL5000
#define SK_AUDIO_SGTL5000 0
#endif
#if SK_PRIMARY_RA8875
#include <RA8875.h>
// RA8875 exports a macro that collides with ST7735_t3::CENTER.
#undef CENTER
static RA8875 primary(superkeyboard::primary_cs, superkeyboard::primary_reset);
static bool primary_ready;
#endif
#if SK_SECONDARY_ST7735
#include <ST7735_t3.h>
static ST7735_t3 secondary(superkeyboard::secondary_cs, superkeyboard::secondary_dc,
    superkeyboard::shared_mosi, superkeyboard::shared_sck, superkeyboard::secondary_reset);
#endif

// All display calls are made by the console task. No framebuffer/DMA yet.
void sk_displays_begin() {
#if SK_PRIMARY_RA8875
    if (sk_slot_claim(2, "primary-display") != ESP_OK) return;
    if (!sk_spi_lock(0)) return;
    primary.begin(RA8875_800x480, 16, 4000000, 2000000);
    primary_ready = primary.errorCode() == 0;
    if (primary_ready) {
        primary.displayOn(true);
        primary.clearScreen(RA8875_BLACK);
        primary.setTextColor(RA8875_WHITE, RA8875_BLACK);
        primary.setCursor(0, 0);
        primary.print("SolarOS Teensy 4.1 bring-up\r\n");
    } else sk_console_print("RA8875 initialization failed\r\n");
    sk_spi_unlock(0);
#endif
#if SK_SECONDARY_ST7735
    if (!sk_spi_lock(1)) return;
    secondary.initR(INITR_BLACKTAB);
    secondary.fillScreen(ST7735_BLACK);
    secondary.setTextColor(ST7735_WHITE);
    secondary.setCursor(0, 0);
    secondary.print("SolarOS\nTeensy 4.1");
    pinMode(superkeyboard::secondary_backlight, OUTPUT);
    analogWrite(superkeyboard::secondary_backlight, 128);
    sk_spi_unlock(1);
#endif
}
void sk_display_write(const char *text, size_t length) {
#if SK_PRIMARY_RA8875
    if (primary_ready && sk_spi_lock(0)) {
        primary.write(reinterpret_cast<const uint8_t *>(text), length);
        sk_spi_unlock(0);
    }
#else
    (void)text; (void)length;
#endif
}

#if SK_USB_HOST
#include <USBHost_t36.h>
static USBHost host;
static USBHub hub1(host), hub2(host);
static KeyboardController keyboard(host);
static uint8_t keys[64];
static unsigned head, tail;
static void key_pressed(int unicode) {
    // Bootstrap console is ASCII; the full SolarOS input service is not wired yet.
    if (unicode <= 0 || unicode > 127) return;
    const unsigned next = (head + 1) % sizeof(keys);
    if (next != tail) { keys[head] = unicode; head = next; }
}
#endif
void sk_usb_begin() {
#if SK_USB_HOST
    keyboard.attachPress(key_pressed);
    host.begin();
#endif
}
void sk_usb_poll() {
#if SK_USB_HOST
    host.Task();
#endif
}
int sk_usb_read() {
#if SK_USB_HOST
    if (head != tail) {
        int key = keys[tail]; tail = (tail + 1) % sizeof(keys); return key;
    }
#endif
    return -1;
}

#if SK_AUDIO_SGTL5000
#include <Audio.h>
static AudioSynthWaveformSine sine;
static AudioOutputI2S audio_output;
static AudioConnection left(sine, 0, audio_output, 0);
static AudioConnection right(sine, 0, audio_output, 1);
static AudioControlSGTL5000 codec;
static bool audio_ready;
#endif
void sk_audio_begin() {
#if SK_AUDIO_SGTL5000
    AudioMemory(16);
    sine.amplitude(0);
    sine.frequency(440);
    if (!sk_i2c_lock(0)) return;
    audio_ready = codec.enable();
    if (audio_ready) {
        codec.volume(0.2f);
        codec.muteLineout();
    }
    sk_i2c_unlock(0);
    sk_console_print(audio_ready ? "SGTL5000 enabled (headphones, muted tone)\r\n" : "SGTL5000 failed\r\n");
#endif
}
void sk_audio_tone(bool on) {
#if SK_AUDIO_SGTL5000
    if (audio_ready) sine.amplitude(on ? 0.05f : 0.0f);
    else sk_console_print("Audio unavailable\r\n");
#else
    (void)on;
    sk_console_print("Audio disabled in this build\r\n");
#endif
}
