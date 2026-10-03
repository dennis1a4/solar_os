#include <arduino_freertos.h>
#include "board.h"
#include "platform.h"
#if SK_USB_STORAGE
#include "storage_lock.h"
#include "usb_storage.h"
#include "sd_storage.h"
#endif

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
static bool lcd_repaint=true;
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
        lcd->configure(1,7,0);
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
    for(unsigned col=0;col<lcd->cols;++col) out[col]=row<lcd->rows?lcd->cells[row][col].ch:' ';
    out[lcd->cols]=0;
    xSemaphoreGive(lcd_mutex);
}
void sk_lcd_appearance(unsigned &scale,unsigned &fg,unsigned &bg) {
    xSemaphoreTake(lcd_mutex,portMAX_DELAY);
    scale=lcd->scale; fg=lcd->default_fg; bg=lcd->default_bg;
    xSemaphoreGive(lcd_mutex);
}
bool sk_lcd_configure(unsigned scale,unsigned fg,unsigned bg) {
#if SK_PLOT
    if(graphics_mode) return false;
#endif
    xSemaphoreTake(lcd_mutex,portMAX_DELAY);
    bool ok=lcd->configure(scale,fg,bg);
    if(ok) lcd_repaint=true;
    xSemaphoreGive(lcd_mutex);
    return ok;
}
void sk_lcd_flush() {
#if SK_PLOT
    if(graphics_mode) return;
#endif
    if(!lcd || !sk_spi_lock(0)) return;
    static const uint16_t colors[]={0x0000,0xa800,0x0540,0xad40,0x0015,0xa815,0x0555,0xad55,
                                   0x52aa,0xf800,0x07e0,0xffe0,0x001f,0xf81f,0x07ff,0xffff};
    xSemaphoreTake(lcd_mutex,portMAX_DELAY);
    if(lcd_repaint) {
        primary.setFontDefault(); primary.setFontScale(lcd->scale-1);
        primary.clearScreen(colors[lcd->default_bg]);
        memset(lcd->dirty,1,sizeof(lcd->dirty)); lcd_repaint=false;
    }
    unsigned drawn=0;
    for(unsigned checked=0;checked<lcd->rows*lcd->cols && drawn<128;++checked) {
        unsigned pos=render_position++%(lcd->rows*lcd->cols);
        unsigned row=pos/lcd->cols,col=pos%lcd->cols;
        if(!lcd->dirty[row][col]) continue;
        lcd->dirty[row][col]=false; ++drawn;
        const auto cell=lcd->cells[row][col];
        uint16_t fg=colors[(cell.fg==LcdTerminal::default_color?lcd->default_fg:cell.fg) | ((cell.flags&1)?8:0)];
        uint16_t bg=colors[cell.bg==LcdTerminal::default_color?lcd->default_bg:cell.bg];
        bool inverse=cell.flags&4;
        if(lcd->visible && row==lcd->y && col==lcd->x) inverse=!inverse;
        if(inverse) { auto t=fg; fg=bg; bg=t; }
        primary.setTextColor(fg,bg);
        primary.setCursor(col*8*lcd->scale,row*16*lcd->scale);
        primary.write(cell.ch);
        if(cell.flags&2) primary.drawLine(col*8*lcd->scale,(row+1)*16*lcd->scale-1,(col+1)*8*lcd->scale-1,(row+1)*16*lcd->scale-1,fg);
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
#if SK_MIDI
#include "midi_record.h"
static MIDIDevice midi_device(host);
EXTMEM static MidiEvent midi_events[256];
static unsigned midi_head,midi_tail,midi_drops,midi_unsupported;
static bool midi_capture;
bool sk_midi_usb_connected(){return bool(midi_device);}
void sk_midi_usb_capture(bool on){taskENTER_CRITICAL();midi_capture=on;if(on){midi_head=midi_tail=midi_drops=midi_unsupported=0;}taskEXIT_CRITICAL();}
unsigned sk_midi_usb_drops(){return midi_drops;}
unsigned sk_midi_usb_unsupported(){return midi_unsupported;}
bool sk_midi_usb_pop(MidiEvent &event){
    taskENTER_CRITICAL();bool have=midi_head!=midi_tail;
    if(have){event=midi_events[midi_tail];midi_tail=(midi_tail+1)%256;}
    taskEXIT_CRITICAL();return have;
}
bool sk_midi_usb_send(const solar_os_midi_message_t &m){
#if SK_USB_STORAGE
    StorageLock lock;
#endif
    if(!midi_device)return false;
    midi_device.send(m.status<0xf0?m.status&0xf0:m.status,m.data1,m.data2,m.status<0xf0?(m.status&15)+1:0);
    midi_device.send_now();return true;
}
static void midi_poll(){
    for(unsigned i=0;i<128 && midi_device.read();++i){
        if(!midi_capture)continue;
        uint8_t status=midi_device.getType();
        if(status<0xf0)status|=(midi_device.getChannel()-1)&15;
        unsigned len=solar_os_midi_message_length(status);
        if(!len || status==0xf7){++midi_unsupported;continue;}
        MidiEvent e{millis(),{status,midi_device.getData1(),midi_device.getData2(),uint8_t(len)}};
        taskENTER_CRITICAL();unsigned next=(midi_head+1)%256;
        if(next==midi_tail)++midi_drops;else{midi_events[midi_head]=e;midi_head=next;}
        taskEXIT_CRITICAL();
    }
}
#endif
static USBHub hub1(host), hub2(host);
static USBHIDParser hid1(host), hid2(host), hid3(host);
#include "keyboard_input.h"
static TeensyKeyboardInput keyboard_input;
struct KeyboardIrqGuard {
    uint32_t mask=__get_PRIMASK();
    KeyboardIrqGuard() { __disable_irq(); }
    ~KeyboardIrqGuard() { __set_PRIMASK(mask); }
};
class RepeatingKeyboard : public KeyboardController {
public:
    explicit RepeatingKeyboard(USBHost &h):KeyboardController(h) {}
protected:
    bool hid_process_in_data(const Transfer_t *transfer) override {
        const bool handled=KeyboardController::hid_process_in_data(transfer);
        if(handled && transfer->length==8) {
            KeyboardIrqGuard guard;
            keyboard_input.leds=LEDS();
            keyboard_input.boot_report(static_cast<const uint8_t *>(transfer->buffer),millis());
        }
        return handled;
    }
    void hid_input_begin(uint32_t usage,uint32_t type,int min,int max) override {
        variable_keys=(usage==0x10006) && (type&2);
        KeyboardController::hid_input_begin(usage,type,min,max);
    }
    void hid_input_data(uint32_t usage,int32_t value) override {
        {
            KeyboardIrqGuard guard;
            if(usage>=0x700e0 && usage<=0x700e7) {
                const uint8_t bit=1u<<(usage-0x700e0);
                if(value)keyboard_input.modifiers|=bit;
                else keyboard_input.modifiers&=~bit;
            } else if(variable_keys && usage>=0x70004 && usage<=0x70073) {
                keyboard_input.leds=LEDS();
                if(value)keyboard_input.press(uint8_t(usage),millis());
                else keyboard_input.release(uint8_t(usage));
            }
        }
        KeyboardController::hid_input_data(usage,value);
    }
    void disconnect_collection(Device_t *device) override {
        KeyboardController::disconnect_collection(device);
        if(!bool(*this)) { KeyboardIrqGuard guard;keyboard_input.disconnect(); }
    }
private:
    bool variable_keys=false;
};
static RepeatingKeyboard keyboard(host);
bool sk_usb_keyboard_connected() { return bool(keyboard); }
void sk_usb_status(char *out,size_t len) {
    uint32_t presses,repeats,dropped; unsigned key;
    { KeyboardIrqGuard guard;
      key=keyboard_input.repeat_key(); presses=keyboard_input.presses;
      repeats=keyboard_input.repeats; dropped=keyboard_input.dropped; }
    snprintf(out,len,"hub=%u/%u keyboard=%04x:%04x repeat=%u delay=%lu rate=%lu press=%lu repeat-count=%lu drop=%lu",
        unsigned(bool(hub1)),unsigned(bool(hub2)),keyboard.idVendor(),keyboard.idProduct(),
        key,(unsigned long)TeensyKeyboardInput::delay_ms,
        (unsigned long)TeensyKeyboardInput::interval_ms,(unsigned long)presses,
        (unsigned long)repeats,(unsigned long)dropped);
}
// Diagnostic injection is independent of physical key state and disconnects.
static uint8_t keys[512];
static volatile unsigned head, tail;
static bool last_read_physical;
bool sk_usb_last_read_physical() { return last_read_physical; }
void sk_usb_inject(const char *text) {
    KeyboardIrqGuard guard;
    const size_t length=strlen(text),free=(tail+sizeof(keys)-head-1)%sizeof(keys);
    if(length>free)return; // Never enqueue a partial escape sequence/command.
    while(*text) { keys[head]=*text++;head=(head+1)%sizeof(keys); }
}
void sk_usb_input_boundary() { KeyboardIrqGuard guard;keyboard_input.boundary(); }
uint32_t sk_usb_input_generation() { KeyboardIrqGuard guard;return keyboard_input.generation; }
static void key_pressed(int) {
    KeyboardIrqGuard guard;
    keyboard_input.modifiers=keyboard.getModifiers();keyboard_input.leds=keyboard.LEDS();
    keyboard_input.press(keyboard.getOemKey(),millis());
}
static void key_released(uint8_t key) { KeyboardIrqGuard guard;keyboard_input.release(key); }

#endif
void sk_usb_begin() {
#if SK_USB_HOST
    keyboard.attachPress(key_pressed);
    keyboard.attachRawRelease(key_released);
#if SK_USB_STORAGE
    sk_usb_storage_begin();
#endif
    host.begin();
#endif
}
void sk_usb_poll() {
#if SK_USB_HOST
#if SK_USB_STORAGE
    StorageLock lock;
#endif
    host.Task();
#if SK_MIDI
    midi_poll();
#endif
#if SK_USB_STORAGE
    sk_usb_storage_poll();
#if SK_SD_RECOVERY
    sk_sd_poll();
#endif
#endif
#endif
}
int sk_usb_read() {
#if SK_USB_HOST
    KeyboardIrqGuard guard;
    keyboard_input.leds=keyboard.LEDS();
    // Finish a physical escape sequence before allowing another producer in.
    if(keyboard_input.in_sequence()) { last_read_physical=true;return keyboard_input.read(millis()); }
    if(head!=tail) { last_read_physical=false;int key=keys[tail];tail=(tail+1)%sizeof(keys);return key; }
    last_read_physical=true;
    return keyboard_input.read(millis());
#else
    return -1;
#endif
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
    if (!enabled && lcd) {
        xSemaphoreTake(lcd_mutex,portMAX_DELAY);
        lcd_repaint=true;
        xSemaphoreGive(lcd_mutex);
    }
}
extern "C" bool sk_lcd_pixels(int x,int y,int width,int height,const uint16_t *pixels) {
    if(!sk_spi_lock(0)) return false;
    primary.writeRect(x,y,width,height,pixels); sk_spi_unlock(0); return true;
}
extern "C" void sk_lcd_graphics_presented(uint32_t elapsed) {
    ++graphics_frames;graphics_ms=elapsed;
}
#endif
