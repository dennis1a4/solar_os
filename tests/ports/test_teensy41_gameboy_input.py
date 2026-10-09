"""Regression: physical releases must not be extended by terminal key pulses."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
source = (root / 'src/apps/solar_os_gameboy.c').read_text()
mapping = source[source.index('static uint8_t gameboy_button_for_char'):source.index('#if SOLAR_OS_PLATFORM_IMXRT1062\nstatic uint8_t gameboy_input_pressed_mask')]
handling = source[source.index('static uint8_t gameboy_pulse_pressed_mask'):source.index('static bool gameboy_run_frame')]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "solar_os_keys.h"
#define SOLAR_OS_PLATFORM_IMXRT1062 1
#define GAMEBOY_INPUT_PULSE_US 140000LL
#define JOYPAD_A 1
#define JOYPAD_B 2
#define JOYPAD_SELECT 4
#define JOYPAD_START 8
#define JOYPAD_RIGHT 16
#define JOYPAD_LEFT 32
#define JOYPAD_UP 64
#define JOYPAD_DOWN 128
#define ESP_OK 0
#define SOLAR_OS_LOGW(...) ((void)0)
typedef int esp_err_t;
typedef int solar_os_context_t;
static struct {void *control_mutex; bool loaded,paused,reset_requested; uint8_t pressed_mask; int64_t release_at[8];} gameboy;
static uint8_t held;
static bool physical;
static int64_t now;
static bool finished;
static uint8_t gameboy_input_pressed_mask(void){return held;}
static bool sk_gameboy_keyboard_event(void){return physical;}
static void sk_gameboy_trace(char kind,uint8_t value){(void)kind;(void)value;}
static int64_t esp_timer_get_time(void){return now;}
static void gameboy_control_lock(void){}
static void gameboy_control_unlock(void){}
static void solar_os_context_finish(solar_os_context_t*c,int n,void*p){(void)c;(void)n;(void)p;finished=true;}
static void solar_os_gameboy_audio_suspend(void){}
static esp_err_t solar_os_gameboy_audio_resume(void){return ESP_OK;}
''' + mapping + handling + r'''
int main(void){
 gameboy.control_mutex=&gameboy;gameboy.loaded=true;physical=true;
 // Rapid separate taps and repeat characters must follow actual HID state.
 for(int i=0;i<20;i++){
  held=(i%2)?JOYPAD_DOWN:JOYPAD_UP;
  uint8_t key=(i%2)?SOLAR_OS_KEY_DOWN:SOLAR_OS_KEY_UP;
  gameboy_handle_char(NULL,key);assert(gameboy.pressed_mask==held);
  now+=20000;gameboy_handle_char(NULL,key);
  held=0;now+=20000;gameboy_refresh_inputs(now);
  assert(gameboy.pressed_mask==0);
  now+=20000;
 }
 // A queued physical character arriving after release cannot resurrect it.
 gameboy_handle_char(NULL,SOLAR_OS_KEY_UP);assert(gameboy.pressed_mask==0);
 // Injected terminal controls still get a pulse, including with HID held.
 physical=false;held=JOYPAD_A;gameboy_handle_char(NULL,SOLAR_OS_KEY_LEFT);
 assert(gameboy.pressed_mask==(JOYPAD_A|JOYPAD_LEFT));
 held=0;now+=139999;gameboy_refresh_inputs(now);assert(gameboy.pressed_mask==JOYPAD_LEFT);
 now++;gameboy_refresh_inputs(now);assert(gameboy.pressed_mask==0);
 physical=true;gameboy_handle_char(NULL,'p');assert(gameboy.paused);
 gameboy_handle_char(NULL,'p');assert(!gameboy.paused);
 gameboy_handle_char(NULL,'r');assert(gameboy.reset_requested);
 gameboy_handle_char(NULL,'q');assert(finished);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(root/'src/services'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: rapid physical taps, releases, repeat/stale characters, injected pulses and app controls')
