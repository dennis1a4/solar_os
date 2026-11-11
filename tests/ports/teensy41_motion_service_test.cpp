#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "esp_err.h"
extern "C" {
#include "solar_os_stream.h"
#include "solar_os_imu.h"
#include "solar_os_mag.h"
}
uint32_t motion_test_now;
unsigned motion_test_critical;
static uint8_t imu_regs[256],mag_regs[256];
static bool missing_imu,missing_mag;
static unsigned transfers;
extern "C" size_t strlcpy(char *dst,const char *src,size_t size) {
    const size_t length=strlen(src);
    if(size) {const size_t n=length<size-1?length:size-1;memcpy(dst,src,n);dst[n]=0;}
    return length;
}
extern "C" int64_t esp_timer_get_time() {return uint64_t(motion_test_now)*1000;}
extern "C" uint64_t solar_os_time_uptime_ms() {return motion_test_now;}
extern "C" esp_err_t solar_os_time_get_utc_epoch_ms(uint64_t *) {return ESP_ERR_INVALID_STATE;}
esp_err_t sk_i2c_transfer(unsigned bus,uint8_t address,const uint8_t *tx,
    size_t tx_length,uint8_t *rx,size_t rx_length) {
    assert(bus==1 && motion_test_critical==0);
    assert(address==0x6a || address==0x30);
    ++transfers;
    if(address==0x6a?missing_imu:missing_mag)return ESP_ERR_NOT_FOUND;
    uint8_t *regs=address==0x6a?imu_regs:mag_regs;
    if(rx_length) {assert(tx_length==1);memcpy(rx,regs+tx[0],rx_length);}
    else {
        assert(tx_length==2);
        if(address==0x30 && tx[0]==8)regs[8]&=~tx[1];
        else regs[tx[0]]=tx[1];
    }
    return ESP_OK;
}
#define SK_MOTION 1
#include "motion_service.cpp"
static void set_imu_word(unsigned reg,int16_t value) {
    imu_regs[reg]=uint16_t(value)&255;imu_regs[reg+1]=uint16_t(value)>>8;
}
int main() {
    imu_regs[0x0f]=0x6c;mag_regs[0x2f]=0x30;
    imu_regs[0x1e]=3;
    set_imu_word(0x22,1000);set_imu_word(0x28,16384);
    // 18-bit fields of +50, 0 and -50 uT.
    mag_regs[0]=0x88;mag_regs[2]=0x80;mag_regs[4]=0x78;mag_regs[8]=1;
    assert(sk_motion_begin()==ESP_OK);
    assert(sk_motion_begin()==ESP_OK && solar_os_imu_count()==1 && solar_os_mag_count()==1);
    solar_os_imu_sample_t imu;
    assert(solar_os_imu_read_sample("imu0",0,&imu)==ESP_ERR_TIMEOUT);
    motion_test_now=100;poll_sensors();
    assert(solar_os_imu_read_sample("imu0",0,&imu)==ESP_OK);
    assert(imu.valid==(SOLAR_OS_IMU_CAP_ACCELERATION|SOLAR_OS_IMU_CAP_ANGULAR_VELOCITY));
    assert(fabsf(imu.acceleration_m_s2[0]-9.801f)<0.01f);
    assert(fabsf(imu.angular_velocity_rad_s[0]-0.1527163f)<0.00001f);
    const uint64_t timestamp=imu.timestamp_us;
    const unsigned before=transfers;
    solar_os_mag_sample_t mag;
    assert(solar_os_mag_read_sample("mag0",0,&mag)==ESP_OK);
    assert(mag.magnetic_field_ut[0]==50 && mag.magnetic_field_ut[1]==0 && mag.magnetic_field_ut[2]==-50);
    motion_test_now=110;
    assert(solar_os_imu_read_sample("imu0",0,&imu)==ESP_OK && imu.timestamp_us==timestamp);
    assert(transfers==before); // Readers share the worker cache without I2C.
    solar_os_stream_handle_t a{},b{};a.slot=b.slot=-1;
    assert(solar_os_stream_open("imu.imu0","a",&a)==ESP_OK);
    assert(solar_os_stream_open("mag.mag0","b",&b)==ESP_OK);
    uint8_t wire[64];size_t length;
    assert(solar_os_stream_read(&a,wire,sizeof(wire),0,&length)==ESP_OK && length==52);
    assert(solar_os_stream_read(&b,wire,sizeof(wire),0,&length)==ESP_OK && length==24);
    solar_os_stream_close(&a);solar_os_stream_close(&b);
    missing_mag=true;motion_test_now=120;poll_sensors();
    assert(solar_os_mag_read_sample("mag0",0,&mag)==ESP_ERR_NOT_FOUND);
    assert(solar_os_imu_read_sample("imu0",0,&imu)==ESP_OK);
    missing_mag=false;motion_test_now=2120;poll_sensors();
    mag_regs[8]=1;motion_test_now=2220;poll_sensors();
    assert(solar_os_mag_read_sample("mag0",0,&mag)==ESP_OK);
    imu_regs[0x1e]=mag_regs[8]=0;
    motion_test_now=2820;
    assert(solar_os_imu_read_sample("imu0",0,&imu)==ESP_ERR_TIMEOUT);
    assert(solar_os_mag_read_sample("mag0",0,&mag)==ESP_ERR_TIMEOUT);
    assert(solar_os_imu_read_sample("imu0",25,&imu)==ESP_ERR_TIMEOUT && motion_test_now==2845);
    missing_imu=missing_mag=true;poll_sensors();
    assert(solar_os_imu_read_sample("imu0",1000,&imu)==ESP_ERR_NOT_FOUND);
    assert(solar_os_mag_read_sample("mag0",1000,&mag)==ESP_ERR_NOT_FOUND);
    assert(motion_test_critical==0);
    puts("PASS: shared motion worker, SI units, timestamps, typed streams, independent recovery, cache expiry and bounded reads");
}
