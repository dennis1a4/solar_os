#include <arduino_freertos.h>
#include <thread>
#include <mutex>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
extern "C" {
#include "solar_os_audio_player.h"
#include "solar_os_memory.h"
#include "solar_os_task.h"
}
static std::recursive_mutex guard;
static std::atomic<unsigned> allocations;
static std::atomic<bool> paused;
static bool present=true,fail_task=false;
static std::vector<int16_t> output;
struct Suspended {};
extern "C" void radio_enter(){guard.lock();}
extern "C" void radio_exit(){guard.unlock();}
extern "C" void vTaskDelay(TickType_t n){std::this_thread::sleep_for(std::chrono::milliseconds(n));}
extern "C" void vTaskSuspend(TaskHandle_t){throw Suspended{};}
extern "C" void *solar_os_memory_alloc(size_t n,solar_os_memory_class_t kind,const char *){assert(kind==SOLAR_OS_MEMORY_EXTERNAL_REQUIRED);void *p=malloc(n);if(p)++allocations;return p;}
extern "C" void *solar_os_memory_calloc(size_t n,size_t size,solar_os_memory_class_t kind,const char *tag){void *p=solar_os_memory_alloc(n*size,kind,tag);if(p)memset(p,0,n*size);return p;}
extern "C" void solar_os_memory_free(void *p){if(p){--allocations;free(p);}}
extern "C" BaseType_t solar_os_task_create_pinned_external(TaskFunction_t fn,const char *,uint32_t,void *arg,UBaseType_t,TaskHandle_t *out,BaseType_t,solar_os_task_role_t){
    if(fail_task)return pdFAIL;
    *out=new std::thread([=]{try{fn(arg);}catch(Suspended &) {}});return pdPASS;
}
extern "C" bool solar_os_task_wait_done(TaskHandle_t,volatile bool *done,uint32_t timeout){while(timeout-- && !*done)vTaskDelay(1);return *done;}
extern "C" void solar_os_task_delete_external(TaskHandle_t task){auto *t=static_cast<std::thread *>(task);t->join();delete t;}
extern "C" esp_err_t sk_audio_output_start(uint8_t){return present?ESP_OK:ESP_ERR_NOT_FOUND;}
extern "C" esp_err_t sk_audio_output_finish(bool){return ESP_OK;}
extern "C" esp_err_t sk_audio_output_volume(uint8_t){return ESP_OK;}
extern "C" esp_err_t sk_audio_synth_write(const int16_t *data,size_t frames,const volatile bool *stop){if(*stop)return ESP_ERR_TIMEOUT;output.insert(output.end(),data,data+frames*2);vTaskDelay(1);return ESP_OK;}
static bool is_paused(void *){return paused;}
int main(){
    solar_os_audio_player_options_t options{};options.target_ms=1;options.should_pause=is_paused;
    solar_os_audio_player_t *p=nullptr;solar_os_stream_audio_format_t format{};solar_os_audio_device_info_t device{};
    present=false;assert(solar_os_audio_player_create(&options,&p,&format,&device)==ESP_ERR_NOT_FOUND && !p && !allocations);
    present=true;fail_task=true;assert(solar_os_audio_player_create(&options,&p,&format,&device)==ESP_ERR_NO_MEM && !allocations);fail_task=false;
    assert(solar_os_audio_player_create(&options,&p,&format,&device)==ESP_OK);
    assert(format.sample_rate==44100 && format.channels==2);
    std::vector<int16_t> input(200000);for(size_t i=0;i<input.size();++i)input[i]=i;
    assert(solar_os_audio_player_write(p,input.data(),input.size()*2,nullptr)==ESP_OK);
    assert(solar_os_audio_player_finish(p,nullptr)==ESP_OK);
    solar_os_audio_player_destroy(p);assert(output==input && !allocations);
    paused=true;assert(solar_os_audio_player_create(&options,&p,&format,&device)==ESP_OK);
    volatile bool cancelled=true;assert(solar_os_audio_player_write(p,input.data(),4,&cancelled)==ESP_ERR_TIMEOUT);
    solar_os_audio_player_destroy(p);assert(!allocations);
    puts("PASS: radio missing hardware, worker failure cleanup, ring wrap/backpressure, byte-exact PCM, pause/cancel and full reclamation");
}
