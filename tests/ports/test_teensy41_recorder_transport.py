"""Run the actual Teensy capture/file transport with deterministic microphone input."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class RecorderTransportTest(unittest.TestCase):
    def test_capture_contract(self):
        source = (ROOT / 'src/platform/imxrt1062/teensy41/audio_files.c').read_text()
        body = source[source.index('static void put32('):source.rindex('#endif')]
        prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
typedef int esp_err_t;
enum {ESP_OK,ESP_FAIL,ESP_ERR_TIMEOUT,ESP_ERR_INVALID_ARG,ESP_ERR_NOT_SUPPORTED,ESP_ERR_INVALID_STATE,ESP_ERR_NO_MEM};
#define SOLAR_OS_MEMORY_EXTERNAL_REQUIRED 0
#define SOLAR_OS_AUDIO_WAV_MAX_MS 3600000U
typedef struct {uint32_t sample_rate,data_bytes,duration_ms;uint16_t block_align;uint8_t channels,bits_per_sample;} solar_os_audio_wav_info_t;
typedef struct {solar_os_audio_wav_info_t info;bool done,cancelled;} solar_os_audio_wav_progress_t;
typedef struct {
 const char *capture_stream;
 struct {uint32_t sample_rate;uint8_t channels,bits_per_sample;} record_format;
 bool monitor;uint8_t monitor_volume;
 bool (*should_cancel)(void*),(*should_pause)(void*),(*should_monitor)(void*);
 void (*samples)(const int16_t*,size_t,uint8_t,void*);
 void (*progress)(const solar_os_audio_wav_progress_t*,void*);
 void *user;uint32_t progress_interval_ms;
} solar_os_audio_wav_options_t;
static unsigned reads,allocations,sample_count,progress_calls,output_frames,stops;
static unsigned capture_error,drop_count;
static bool sync_error;
static void *solar_os_memory_alloc(size_t n,int cls,const char *tag){(void)cls;(void)tag;void*p=malloc(n);if(p)allocations++;return p;}
static void solar_os_memory_free(void*p){if(p){allocations--;free(p);}}
static bool cancelled(const solar_os_audio_wav_options_t*o){return o&&o->should_cancel&&o->should_cancel(o->user);}
static int sk_audio_capture_start(void){reads=0;return ESP_OK;}
static int sk_audio_capture_start_buffered(void){return sk_audio_capture_start();}
static int sk_audio_capture_read(int16_t*p,size_t cap,size_t*n){assert(cap>=128);reads++;if(capture_error&&reads==capture_error)return ESP_FAIL;*n=128;for(unsigned i=0;i<128;i++)p[i]=reads;return ESP_OK;}
static unsigned sk_audio_capture_stop(void){stops++;return drop_count;}
static int playback_start(uint8_t v,const solar_os_audio_wav_options_t*o){(void)v;(void)o;return ESP_OK;}
static int sk_audio_output_finish(bool drain){(void)drain;return ESP_OK;}
static int sk_audio_output_write(const int16_t*p,size_t frames){for(size_t i=0;i<frames;i++)assert(p[2*i]==p[2*i+1]);output_frames+=frames;return ESP_OK;}
static int solar_os_storage_sync_file(FILE*f){return sync_error?ESP_FAIL:(fflush(f)?ESP_FAIL:ESP_OK);}
'''
        suffix = r'''
static bool cancel_cb(void*u){(void)u;return reads>=12;}
static bool pause_cb(void*u){(void)u;return reads>=3&&reads<=6;}
static bool monitor_cb(void*u){(void)u;return reads>=8;}
static void samples_cb(const int16_t*p,size_t n,uint8_t ch,void*u){(void)u;assert(ch==1);assert(p[0]<3||p[0]>6);sample_count+=n;}
static void progress_cb(const solar_os_audio_wav_progress_t*p,void*u){(void)u;assert(p->info.data_bytes>0);progress_calls++;}
static unsigned get32(unsigned char*p){return p[0]|p[1]<<8|p[2]<<16|p[3]<<24;}
static void check_wav(const char*path,unsigned bytes){FILE*f=fopen(path,"rb");assert(f);unsigned char h[44];assert(fread(h,1,44,f)==44);assert(!memcmp(h,"RIFF",4)&&get32(h+4)==bytes+36&&get32(h+40)==bytes);assert(get32(h+24)==44100&&h[22]==1&&h[34]==16);fseek(f,0,SEEK_END);assert(ftell(f)==44+bytes);fclose(f);assert(allocations==0);}
int main(void){
 solar_os_audio_wav_options_t o={.capture_stream="mic",.should_cancel=cancel_cb,.should_pause=pause_cb,.should_monitor=monitor_cb,.samples=samples_cb,.progress=progress_cb,.progress_interval_ms=1};
 solar_os_audio_wav_info_t info;
 assert(solar_os_audio_record_wav("take.wav",0,&o,&info)==ESP_ERR_TIMEOUT);
 assert(info.data_bytes==8*128*2&&sample_count==8*128&&output_frames==5*128&&progress_calls>0&&stops==1);
 check_wav("take.wav",8*128*2);
 assert(solar_os_audio_record_wav("take.wav",0,&o,&info)==ESP_ERR_INVALID_STATE);check_wav("take.wav",8*128*2);
 assert(solar_os_audio_record_wav("missing/take.wav",0,&o,&info)==ESP_FAIL&&allocations==0);
 o.record_format.sample_rate=16000;
 assert(solar_os_audio_record_wav("bad.wav",0,&o,&info)==ESP_ERR_NOT_SUPPORTED);
 o.record_format.sample_rate=0;o.samples=NULL;capture_error=3;
 assert(solar_os_audio_record_wav("error.wav",0,&o,&info)==ESP_FAIL);check_wav("error.wav",2*128*2);
 capture_error=0;drop_count=1;o.should_cancel=NULL;o.should_pause=NULL;
 assert(solar_os_audio_record_wav("drop.wav",10,&o,&info)==ESP_FAIL);check_wav("drop.wav",441*2);
 drop_count=0;sync_error=true;
 assert(solar_os_audio_record_wav("sync.wav",10,&o,&info)==ESP_FAIL);assert(allocations==0);
 sync_error=false;o.should_cancel=cancel_cb;
 assert(solar_os_audio_monitor_stream(20,&o,&info)==ESP_ERR_TIMEOUT&&allocations==0);
 return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='recorder-transport-') as tmp:
            path = Path(tmp)
            (path/'test.c').write_text(prefix+body+suffix)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-g',str(path/'test.c'),
                            '-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],cwd=path,check=True)

if __name__ == '__main__':
    unittest.main()
