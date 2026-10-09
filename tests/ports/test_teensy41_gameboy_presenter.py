"""Exercise actual small-panel presenter scaling and ownership with a mock SPI sink."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'src/platform/imxrt1062/teensy41/gameboy_presenter.cpp').read_text()
source=source[source.index('static uint16_t *pixels;'):source.rindex('#endif')]
code=r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
using esp_err_t=int;
struct solar_os_gfx_t{};
struct solar_os_gameboy_presenter_stats_t {uint64_t present_us; uint32_t presented_frames,dropped_frames; esp_err_t last_error;};
constexpr int ESP_OK=0,ESP_ERR_INVALID_STATE=1,ESP_ERR_NO_MEM=2,ESP_ERR_TIMEOUT=3;
constexpr int SOLAR_OS_MEMORY_INTERNAL_PREFERRED=0;
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
static unsigned allocations,draws;
static bool owned,fail_alloc,fail_spi;
static uint16_t frame[160*128];
#define strlcpy test_strlcpy
static size_t strlcpy(char *d,const char *s,size_t n){if(n)snprintf(d,n,"%s",s);return strlen(s);}
static uint32_t micros(){static uint32_t now=0;return now+=20;}
static uint32_t millis(){return micros()/1000;}
static void *solar_os_memory_calloc(size_t n,size_t s,int,const char*){if(fail_alloc)return nullptr;++allocations;return calloc(n,s);}
static void solar_os_memory_free(void *p){if(p){assert(allocations);--allocations;free(p);}}
static bool sk_small_acquire(){if(owned)return false;return owned=true;}
static void sk_small_release(){assert(owned);owned=false;}
static bool sk_small_frame(const uint16_t *p){assert(owned);if(fail_spi)return false;memcpy(frame,p,sizeof(frame));++draws;return true;}
''' + source + r'''
int main(){
 fail_alloc=true;assert(solar_os_gameboy_presenter_init(nullptr)==ESP_ERR_NO_MEM);fail_alloc=false;
 owned=true;assert(solar_os_gameboy_presenter_init(nullptr)==ESP_ERR_INVALID_STATE);assert(!allocations);owned=false;
 assert(solar_os_gameboy_presenter_init(nullptr)==ESP_OK);assert(allocations==1);
 uint8_t bitmap[160*144/4];
 for(unsigned y=0;y<144;++y)for(unsigned x=0;x<40;++x)bitmap[y*40+x]=((x+y)%4)*0x55;
 assert(!solar_os_gameboy_presenter_queue(nullptr));assert(solar_os_gameboy_presenter_queue(bitmap));
 const uint16_t palette[]={0xffff,0xad55,0x52aa,0};
 for(unsigned y=0;y<128;++y)for(unsigned x=0;x<160;++x){
  const uint16_t expected=x<9 || x>=151?0:palette[(((x-9)*160/142)/4+y*144/128)%4];
  assert(frame[y*160+x]==expected);
 }
 fail_spi=true;assert(!solar_os_gameboy_presenter_queue(bitmap));fail_spi=false;
 solar_os_gameboy_presenter_stats_t stats{};solar_os_gameboy_presenter_take_stats(&stats);
 assert(stats.presented_frames==1 && stats.dropped_frames==1 && stats.last_error==ESP_ERR_TIMEOUT);
 solar_os_gameboy_presenter_suspend();assert(!owned && !solar_os_gameboy_presenter_queue(bitmap));
 assert(solar_os_gameboy_presenter_resume()==ESP_OK);assert(solar_os_gameboy_presenter_queue(bitmap));
 solar_os_gameboy_presenter_deinit();assert(!allocations && !owned);
 solar_os_gameboy_presenter_deinit();assert(solar_os_gameboy_presenter_resume()==ESP_ERR_INVALID_STATE);
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: Game Boy downscaling, borders, allocation failures, SPI failure, ownership and cleanup')
