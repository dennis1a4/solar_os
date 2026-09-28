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
#ifndef SK_PRIMARY_PANEL
#define SK_PRIMARY_PANEL RA8875_800x480
#endif
#ifndef SK_PRIMARY_ROTATION
#define SK_PRIMARY_ROTATION 0
#endif
// RA8875 exports a macro that collides with ST7735_t3::CENTER.
#undef CENTER
static RA8875 primary(superkeyboard::primary_cs, superkeyboard::primary_reset,
    superkeyboard::primary_mosi, superkeyboard::primary_sck, superkeyboard::primary_miso);
static bool primary_ready;
#if SK_LCD_CONSOLE
#include "lcd_terminal.h"
#include <semphr.h>
extern "C" {
#include "solar_os_memory.h"
}
static LcdTerminal *lcd;
static SemaphoreHandle_t lcd_mutex;
static unsigned render_position;
#if SK_PLOT
static bool graphics_mode;
static uint32_t graphics_frames,graphics_ms;
extern "C" void sk_gfx_status(char *out,size_t size) { snprintf(out,size,"graphics=%s frames=%lu render-ms=%lu",graphics_mode?"active":"idle",(unsigned long)graphics_frames,(unsigned long)graphics_ms); }
#endif
#endif
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
    // Temporary CS wiring may reuse another expansion connector's select pin.
    // Reserve it as well so slot SPI operations cannot select the LCD.
    for (unsigned slot = 0; slot < 2; ++slot) {
        if (superkeyboard::slots[slot].cs == superkeyboard::primary_cs &&
            sk_slot_claim(slot, "primary-display") != ESP_OK) {
            sk_console_print("RA8875 chip select is already in use\r\n");
            return;
        }
    }
    if (!sk_spi_lock(0)) return;
    primary.begin(SK_PRIMARY_PANEL, 16, 4000000, 2000000);
    primary_ready = primary.errorCode() == 0;
    if (primary_ready) {
        primary.setRotation(SK_PRIMARY_ROTATION);
        primary.displayOn(true);
        primary.clearScreen(RA8875_BLACK);
        primary.setTextColor(RA8875_WHITE, RA8875_BLACK);
        primary.setCursor(0, 0);
        primary.print("SolarOS Teensy 4.1 bring-up\r\n");
#if SK_LCD_CONSOLE
        lcd_mutex=xSemaphoreCreateMutex();
        lcd=static_cast<LcdTerminal *>(solar_os_memory_calloc(1,sizeof(LcdTerminal),
            SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"lcd-terminal"));
        configASSERT(lcd_mutex && lcd);
        lcd->reset();
        primary.setFontDefault();
        primary.setFontScale(0);
        primary.showCursor(NOCURSOR,false);
#endif
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

#if SK_LCD_CONSOLE
bool sk_lcd_ready() { return primary_ready && lcd; }
void sk_lcd_write(const char *text,size_t length) {
    if(!lcd) return;
    xSemaphoreTake(lcd_mutex,portMAX_DELAY);
    lcd->write(text,length);
    xSemaphoreGive(lcd_mutex);
}
void sk_lcd_row(unsigned row,char *out) {
    xSemaphoreTake(lcd_mutex,portMAX_DELAY);
    for(unsigned col=0;col<LcdTerminal::cols;++col) out[col]=lcd->cells[row][col].ch;
    out[LcdTerminal::cols]=0;
    xSemaphoreGive(lcd_mutex);
}
void sk_lcd_flush() {
#if SK_PLOT
    if(graphics_mode) return;
#endif
    if(!lcd || !sk_spi_lock(0)) return;
    static const uint16_t colors[]={0x0000,0xa800,0x0540,0xad40,0x0015,0xa815,0x0555,0xad55,
                                   0x52aa,0xf800,0x07e0,0xffe0,0x001f,0xf81f,0x07ff,0xffff};
    xSemaphoreTake(lcd_mutex,portMAX_DELAY);
    unsigned drawn=0;
    for(unsigned checked=0;checked<LcdTerminal::rows*LcdTerminal::cols && drawn<128;++checked) {
        unsigned pos=render_position++%(LcdTerminal::rows*LcdTerminal::cols);
        unsigned row=pos/LcdTerminal::cols,col=pos%LcdTerminal::cols;
        if(!lcd->dirty[row][col]) continue;
        lcd->dirty[row][col]=false; ++drawn;
        const auto cell=lcd->cells[row][col];
        uint16_t fg=colors[cell.fg | ((cell.flags&1)?8:0)], bg=colors[cell.bg];
        bool inverse=cell.flags&4;
        if(lcd->visible && row==lcd->y && col==lcd->x) inverse=!inverse;
        if(inverse) { auto t=fg; fg=bg; bg=t; }
        primary.setTextColor(fg,bg);
        primary.setCursor(col*8,row*16);
        primary.write(cell.ch);
        if(cell.flags&2) primary.drawLine(col*8,row*16+15,col*8+7,row*16+15,fg);
    }
    xSemaphoreGive(lcd_mutex);
    sk_spi_unlock(0);
}
#endif
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
static USBHIDParser hid1(host), hid2(host), hid3(host);
static KeyboardController keyboard(host);
bool sk_usb_keyboard_connected() { return bool(keyboard); }
void sk_usb_status(char *out,size_t len) {
    snprintf(out,len,"host port=%08lx status=%08lx command=%08lx hub=%u/%u keyboard=%04x:%04x",
        (unsigned long)USB2_PORTSC1,(unsigned long)USB2_USBSTS,(unsigned long)USB2_USBCMD,
        unsigned(bool(hub1)),unsigned(bool(hub2)),keyboard.idVendor(),keyboard.idProduct());
}
static uint8_t keys[512];
static volatile unsigned head, tail;
static void enqueue_key(uint8_t key) {
    const unsigned next=(head+1)%sizeof(keys);
    if(next!=tail) { keys[head]=key; head=next; }
}
void sk_usb_inject(const char *text) {
    // HID callbacks may run in the host ISR; keep diagnostic and escape-key
    // sequences atomic with respect to that producer.
    const uint32_t mask=__get_PRIMASK(); __disable_irq();
    while(*text) enqueue_key(*text++);
    __set_PRIMASK(mask);
}
static void key_pressed(int unicode) {
    const char *seq=nullptr;
    switch(unicode) {
    case KEYD_UP: seq="\033[A"; break; case KEYD_DOWN: seq="\033[B"; break;
    case KEYD_RIGHT: seq="\033[C"; break; case KEYD_LEFT: seq="\033[D"; break;
    case KEYD_HOME: seq="\033[H"; break; case KEYD_END: seq="\033[F"; break;
    case KEYD_INSERT: seq="\033[2~"; break; case KEYD_DELETE: seq="\033[3~"; break;
    case KEYD_PAGE_UP: seq="\033[5~"; break; case KEYD_PAGE_DOWN: seq="\033[6~"; break;
    case KEYD_F1: seq="\033OP"; break; case KEYD_F2: seq="\033OQ"; break;
    case KEYD_F3: seq="\033OR"; break; case KEYD_F4: seq="\033OS"; break;
    case KEYD_F5: seq="\033[15~"; break; case KEYD_F6: seq="\033[17~"; break;
    case KEYD_F7: seq="\033[18~"; break; case KEYD_F8: seq="\033[19~"; break;
    case KEYD_F9: seq="\033[20~"; break; case KEYD_F10: seq="\033[21~"; break;
    case KEYD_F11: seq="\033[23~"; break; case KEYD_F12: seq="\033[24~"; break;
    }
    if(seq) { sk_usb_inject(seq); return; }
    if(unicode<=0 || unicode>127) return;
    if((keyboard.getModifiers()&0x11) && unicode>='@' && unicode<='~') unicode&=31;
    enqueue_key(unicode);
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

#if SK_AUDIO_SGTL5000 && !SK_AUDIO_PLAYER
#include <Audio.h>
static AudioSynthWaveformSine sine;
static AudioOutputI2S audio_output;
static AudioConnection left(sine, 0, audio_output, 0);
static AudioConnection right(sine, 0, audio_output, 1);
static AudioControlSGTL5000 codec;
static bool audio_ready;
#endif
void sk_audio_begin() {
#if SK_AUDIO_PLAYER
    extern void sk_audio_player_begin();
    sk_audio_player_begin();
#elif SK_AUDIO_SGTL5000
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
#if SK_AUDIO_PLAYER
    extern void sk_audio_player_tone(bool);
    sk_audio_player_tone(on);
#elif SK_AUDIO_SGTL5000
    if (audio_ready) sine.amplitude(on ? 0.05f : 0.0f);
    else sk_console_print("Audio unavailable\r\n");
#else
    (void)on;
    sk_console_print("Audio disabled in this build\r\n");
#endif
}

#if SK_PLOT
extern "C" void sk_lcd_graphics_mode(bool enabled) {
    graphics_mode=enabled;
    if (!enabled && lcd) memset(lcd->dirty,1,sizeof(lcd->dirty));
}
extern "C" void sk_lcd_pixels(int x,int y,int width,int height,const uint16_t *pixels) {
    if(sk_spi_lock(0)) { primary.writeRect(x,y,width,height,pixels); sk_spi_unlock(0); }
}
extern "C" void sk_lcd_graphics_presented(uint32_t elapsed) {
    ++graphics_frames;graphics_ms=elapsed;
}
#endif
