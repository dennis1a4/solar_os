#if SK_WEBRADIO
#include <arduino_freertos.h>
#include "audio_output.h"
extern "C" {
#include "solar_os_audio_player.h"
#include "solar_os_memory.h"
#include "solar_os_task.h"
}
struct solar_os_audio_player {
    solar_os_audio_player_options_t options;
    uint8_t *buffer;
    size_t capacity,read,write;
    volatile size_t used;
    volatile bool stop,done,eof;
    volatile esp_err_t error;
    TaskHandle_t task;
};
static uint8_t radio_volume=20;
static void sink(void *arg) {
    auto *p=static_cast<solar_os_audio_player *>(arg);
    bool playing=false;
    const size_t target=min(p->capacity/2,size_t(44100*4)*p->options.target_ms/1000);
    while(!p->stop && p->error==ESP_OK) {
        if(p->options.should_pause && p->options.should_pause(p->options.pause_user)){vTaskDelay(1);continue;}
        if(!playing && (p->used>=target || p->eof)) {
            playing=true;if(p->options.state)p->options.state(true,p->options.user);
        }
        if(!playing || !p->used) {if(p->eof)break;vTaskDelay(1);continue;}
        size_t n=min(size_t(p->used),min(p->capacity-p->read,size_t(1024)));n-=n%4;
        if(!n){vTaskDelay(1);continue;}
        p->error=sk_audio_synth_write(reinterpret_cast<int16_t *>(p->buffer+p->read),n/4,&p->stop);
        if(p->options.samples && p->error==ESP_OK)p->options.samples(reinterpret_cast<int16_t *>(p->buffer+p->read),n/2,2,p->options.user);
        taskENTER_CRITICAL();p->read=(p->read+n)%p->capacity;p->used-=n;taskEXIT_CRITICAL();
    }
    sk_audio_output_finish(false);
    if(p->options.state)p->options.state(false,p->options.user);
    p->done=true;
    for(;;)vTaskSuspend(nullptr);
}
extern "C" esp_err_t solar_os_audio_player_create(const solar_os_audio_player_options_t *options,
    solar_os_audio_player_t **out,solar_os_stream_audio_format_t *format,solar_os_audio_device_info_t *device) {
    if(!options || !out || !format || !device)return ESP_ERR_INVALID_ARG;
    *out=nullptr;
    auto *p=static_cast<solar_os_audio_player *>(solar_os_memory_calloc(1,sizeof(solar_os_audio_player),SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"radio.player"));
    if(!p)return ESP_ERR_NO_MEM;
    p->options=*options;p->capacity=128*1024;
    p->buffer=static_cast<uint8_t *>(solar_os_memory_alloc(p->capacity,SOLAR_OS_MEMORY_EXTERNAL_REQUIRED,"radio.jitter"));
    esp_err_t err=p->buffer?sk_audio_output_start(radio_volume):ESP_ERR_NO_MEM;
    if(err==ESP_OK && solar_os_task_create_pinned_external(sink,"radio.pcm",4096,p,1,&p->task,0,SOLAR_OS_TASK_ROLE_SYSTEM)!=pdPASS){
        sk_audio_output_finish(false);err=ESP_ERR_NO_MEM;
    }
    if(err!=ESP_OK){solar_os_memory_free(p->buffer);solar_os_memory_free(p);return err;}
    *format={};format->sample_format=SOLAR_OS_STREAM_AUDIO_S16_LE;format->sample_rate=44100;format->channels=2;format->bits_per_sample=16;
    *device={};strlcpy(device->id,"sgtl5000",sizeof(device->id));
    device->capabilities=SOLAR_OS_AUDIO_DEVICE_CAP_OUTPUT|SOLAR_OS_AUDIO_DEVICE_CAP_VOLUME;
    device->native_format=*format;*out=p;return ESP_OK;
}
extern "C" esp_err_t solar_os_audio_player_write(solar_os_audio_player_t *p,const void *data,size_t len,const volatile bool *cancelled) {
    if(!p || (!data && len) || len%4 || p->eof)return ESP_ERR_INVALID_ARG;
    auto *in=static_cast<const uint8_t *>(data);
    while(len){
        if(p->stop || (cancelled && *cancelled))return ESP_ERR_TIMEOUT;
        if(p->error!=ESP_OK)return p->error;
        size_t space=p->capacity-p->used;
        if(!space){vTaskDelay(1);continue;}
        size_t n=min(len,min(space,p->capacity-p->write));
        memcpy(p->buffer+p->write,in,n);
        taskENTER_CRITICAL();p->write=(p->write+n)%p->capacity;p->used+=n;taskEXIT_CRITICAL();
        in+=n;len-=n;
    }
    return ESP_OK;
}
extern "C" esp_err_t solar_os_audio_player_error(const solar_os_audio_player_t *p){return p?p->error:ESP_ERR_INVALID_ARG;}
extern "C" void solar_os_audio_player_destroy(solar_os_audio_player_t *p){
    if(!p)return;p->stop=true;
    while(!solar_os_task_wait_done(p->task,&p->done,1000))vTaskDelay(1);
    solar_os_task_delete_external(p->task);solar_os_memory_free(p->buffer);solar_os_memory_free(p);
}
extern "C" esp_err_t solar_os_audio_player_end_input(solar_os_audio_player_t *p){if(!p)return ESP_ERR_INVALID_ARG;p->eof=true;return ESP_OK;}
extern "C" esp_err_t solar_os_audio_player_finish(solar_os_audio_player_t *p,const volatile bool *cancelled){
    if(!p)return ESP_ERR_INVALID_ARG;p->eof=true;
    while(!p->done){if(cancelled && *cancelled){p->stop=true;return ESP_ERR_TIMEOUT;}vTaskDelay(1);}return p->error;
}
extern "C" esp_err_t solar_os_audio_set_volume(uint8_t volume){
    esp_err_t err=sk_audio_output_volume(volume);if(err==ESP_OK)radio_volume=volume;return err;
}
extern "C" esp_err_t solar_os_audio_set_device_volume(const char *id,uint8_t volume){return id && !strcmp(id,"sgtl5000")?solar_os_audio_set_volume(volume):ESP_ERR_NOT_FOUND;}
extern "C" void solar_os_audio_get_status(solar_os_audio_status_t *status){if(status){*status={};status->volume=radio_volume;status->sample_rate=44100;status->channels=2;status->bits_per_sample=16;}}
#endif
