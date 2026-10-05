#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <map>
using std::min;
#define SK_CLOCK 1
#define SK_SYNTH 1
#define SK_LCD_CONSOLE 1
#define AUDIO_BLOCK_SAMPLES 128
#define AUDIO_SAMPLE_RATE_EXACT 44100.0f
#define AUDIO_INPUT_MIC 0
#define pdMS_TO_TICKS(x) (x)
using esp_err_t = int;
enum { ESP_OK, ESP_FAIL, ESP_ERR_NOT_FOUND, ESP_ERR_TIMEOUT, ESP_ERR_NO_MEM,
       ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG, SOLAR_OS_MEMORY_INTERNAL_PREFERRED };
static bool interrupts = true, fail_alloc, i2c_ok = true, codec_ok = true;
static bool app_busy;
extern "C" bool sk_console_audio_busy() {return app_busy;}
static bool cancelled, connected = true, run_updates = true;
static uint32_t ticks;
static size_t live;
static std::map<void *,size_t> allocations;
static void (*on_enable)();
static void update_audio();
static void AudioNoInterrupts() { assert(interrupts); interrupts = false; }
static void AudioInterrupts() { assert(!interrupts); interrupts = true; if(on_enable)on_enable(); }
static void __DMB() {}
static uint32_t millis() { return ticks; }
static void vTaskDelay(unsigned n) { ticks += n; if(run_updates)update_audio(); }
static void *solar_os_memory_alloc(size_t n,int kind,const char *) {
    assert(interrupts && kind == SOLAR_OS_MEMORY_INTERNAL_PREFERRED);
    if(fail_alloc)return nullptr;
    void *p=malloc(n);assert(p);allocations[p]=n;live+=n;return p;
}
static void solar_os_memory_free(void *p) {
    assert(interrupts);
    if(!p)return;
    assert(allocations.count(p));live-=allocations[p];allocations.erase(p);free(p);
}
static bool sk_i2c_lock(int) { return i2c_ok; }
static void sk_i2c_unlock(int) {}
static void sk_console_print(const char *) {}
template<class... T> static void sk_console_printf(const char *,T...) {}
static void AudioMemory(unsigned) {}
struct audio_block_t { int16_t data[AUDIO_BLOCK_SAMPLES]; };
static bool input_pending;
static audio_block_t incoming, transmitted;
static unsigned transmissions;
class AudioStream {
public:
    AudioStream(int, audio_block_t **) {}
    virtual void update() = 0;
    audio_block_t *allocate() { return new audio_block_t{}; }
    audio_block_t *receiveReadOnly() {
        if(!input_pending)return nullptr;
        input_pending=false;return new audio_block_t(incoming);
    }
    void transmit(audio_block_t *b,int) {transmitted=*b;++transmissions;}
    void release(audio_block_t *b) {delete b;}
};
struct AudioOutputI2S {};
struct AudioInputI2S {};
struct AudioConnection { template<class A,class B> AudioConnection(A &,int,B &,int) {} };
struct AudioControlSGTL5000 {
    bool enable(){return codec_ok;}
    bool volume(float){return codec_ok;}
    void muteLineout(){}
    bool inputSelect(int){return codec_ok;}
    bool micGain(int){return codec_ok;}
};
extern "C" bool sk_console_poll_cancel(bool) {return cancelled;}
extern "C" bool sk_audio_owner_connected() {return connected;}
extern "C" esp_err_t sk_audio_output_finish(bool);
extern "C" uint32_t sk_audio_capture_stop();
extern "C" bool sk_audio_cancelled();
