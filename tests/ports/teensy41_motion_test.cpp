#include "motion_sensors.h"
#include <cassert>
#include <cstdio>
#include <cstring>
struct Bus {
    uint8_t imu[256]{},mag[256]{};
    bool missing_imu=false,missing_mag=false,fail_write=false;
    unsigned calls=0;
    Bus() {imu[0x0f]=0x6c;mag[0x2f]=0x30;}
    bool read(uint8_t a,uint8_t r,uint8_t *out,size_t n) {
        ++calls;
        if(a==0x6a?missing_imu:missing_mag)return false;
        memcpy(out,(a==0x6a?imu:mag)+r,n);return true;
    }
    bool write(uint8_t a,uint8_t r,uint8_t v) {
        ++calls;
        if(fail_write)return false;
        if(a==0x30 && r==8)mag[r]&=~v;
        else (a==0x6a?imu:mag)[r]=v;
        return true;
    }
    void axis(unsigned reg,int16_t n) {imu[reg]=uint16_t(n)&255;imu[reg+1]=uint16_t(n)>>8;}
    void field(uint32_t x,uint32_t y,uint32_t z) {
        const uint32_t v[]={x,y,z};
        for(unsigned i=0;i<3;++i) {mag[i*2]=v[i]>>10;mag[i*2+1]=(v[i]>>2)&255;}
        mag[6]=((x&3)<<6)|((y&3)<<4)|((z&3)<<2);mag[8]=1;
    }
};
static Bus display_bus;
constexpr unsigned ST7735_BLACK=0,ST7735_WHITE=1,ST7735_GREEN=2,ST7735_YELLOW=3,ST7735_RED=4;
constexpr int ESP_OK=0;
static int sk_i2c_transfer(unsigned bus,uint8_t address,const uint8_t *tx,size_t n,uint8_t *rx,size_t length) {
    assert(bus==1 && n==(length?1u:2u));
    return (length?display_bus.read(address,tx[0],rx,length):display_bus.write(address,tx[0],tx[1]))?0:-1;
}
#ifndef SK_MOTION
#define SK_MOTION 1
#endif
#include "small_motion.h"
struct Display {
    unsigned drawn=0,clears=0;
    void point(int x,int y) {assert(x>=0 && x<160 && y>=56 && y<110);++drawn;}
    void fillRect(int x,int y,int w,int h,unsigned) {point(x,y);point(x+w-1,y+h-1);++clears;}
    void drawCircle(int x,int y,int r,unsigned) {point(x-r,y-r);point(x+r,y+r);}
    void fillCircle(int x,int y,int r,unsigned c) {drawCircle(x,y,r,c);}
    void drawFastHLine(int x,int y,int w,unsigned) {point(x,y);point(x+w-1,y);}
    void drawFastVLine(int x,int y,int h,unsigned) {point(x,y);point(x,y+h-1);}
    void drawLine(int x,int y,int x2,int y2,unsigned) {point(x,y);point(x2,y2);}
};
static bool near(float a,float b,float tolerance=0.01f) {return fabsf(a-b)<tolerance;}
int main() {
    Bus bus;sk_motion::Sensors<Bus> driver(bus);
    driver.poll(0);
    assert(!driver.sample.imu_valid && !driver.sample.mag_valid);
    assert(bus.imu[0x12]==0x44 && bus.imu[0x10]==0x30 && bus.imu[0x11]==0x30);
    assert(bus.mag[9]==0x20 && bus.mag[0xb]==0x8b);
    bus.imu[0x1e]=3;bus.axis(0x22,-1000);bus.axis(0x24,1000);bus.axis(0x26,0);
    bus.axis(0x28,-8192);bus.axis(0x2a,0);bus.axis(0x2c,14189);
    bus.field(131073,131074,131075);driver.poll(100);
    auto s=driver.sample;
    assert(s.imu_fresh(100) && s.mag_fresh(100));
    assert(near(s.acceleration.x,-0.499712f) && near(s.gyro.x,-8.75f));
    assert(near(s.magnetic.x,100.0f/16384,0.00001f));
    assert(near(s.magnetic.y,200.0f/16384,0.00001f));
    assert(near(s.magnetic.z,300.0f/16384,0.00001f));
    float roll,pitch,angle;
    assert(sk_motion::level(s.acceleration,roll,pitch) && near(roll,0) && near(pitch,30,0.1f));
    assert(!sk_motion::level({0,0,0},roll,pitch));
    assert(!sk_motion::level({0,0,2},roll,pitch));
    assert(sk_motion::heading({50,0,0},angle) && near(angle,0));
    assert(sk_motion::heading({0,-50,0},angle) && near(angle,90));
    assert(sk_motion::heading({-50,0,0},angle) && near(angle,180));
    assert(sk_motion::heading({0,50,0},angle) && near(angle,270));
    assert(!sk_motion::heading({0,0,50},angle));
    assert(!sk_motion::heading({1000,0,0},angle));
    assert(near(sk_motion::wrap(-1),359));
    bus.imu[0x1e]=0;driver.poll(700); // Stuck data-ready must age out.
    assert(!driver.sample.imu_fresh(700) && !driver.sample.mag_fresh(700));
    bus.missing_imu=true;bus.field(139264,122880,131072);driver.poll(800);
    assert(!driver.sample.imu_valid && driver.sample.mag_fresh(800));
    bus.missing_imu=false;driver.poll(2800);driver.poll(2900);
    assert(driver.sample.imu_valid==false); // No new IMU data yet.
    bus.imu[0x1e]=3;driver.poll(3000);assert(driver.sample.imu_fresh(3000));
    bus.missing_mag=true;driver.poll(3100);assert(!driver.sample.mag_valid);
    Bus absent;absent.missing_imu=absent.missing_mag=true;
    sk_motion::Sensors<Bus> absent_driver(absent);absent_driver.poll(0);
    const unsigned calls=absent.calls;absent_driver.poll(100);assert(calls==absent.calls);
    absent.missing_imu=absent.missing_mag=false;absent_driver.poll(2000);
    assert(absent.imu[0x10]==0x30 && absent.mag[0xb]==0x8b);
    Bus bad;bad.imu[0x0f]=0;bad.mag[0x2f]=0;
    sk_motion::Sensors<Bus> wrong(bad);wrong.poll(0);assert(!bad.imu[0x10] && !bad.mag[0xb]);
    Bus writes;writes.fail_write=true;sk_motion::Sensors<Bus> failed(writes);failed.poll(0);
    assert(failed.errors==2 && !failed.sample.imu_valid && !failed.sample.mag_valid);
    sk_motion::Sample wrap;wrap.imu_valid=true;wrap.imu_at=0xfffffff0;
    assert(wrap.imu_fresh(20) && !wrap.imu_fresh(1000));
    sk_motion::Calibration calibration;calibration.start();
    for(unsigned i=0;i<60;++i)calibration.add({50,0,0});
    assert(!calibration.finish() && calibration.collecting && !calibration.valid);
    for(unsigned i=0;i<60;++i)calibration.add(i%2?sk_motion::Vector{60,40,80}:sk_motion::Vector{-40,-20,-40});
    assert(calibration.finish() && calibration.valid && !calibration.collecting);
    const auto center=calibration.apply({10,10,20});
    assert(near(center.x,0) && near(center.y,0) && near(center.z,0));
    calibration.start();calibration.add({1,2,3});assert(!calibration.finish());
    assert(calibration.valid); // A failed recalibration preserves the old solution.
    SmallMotion screen;char lines[9][27]{};Display lcd;
#if SK_MOTION
    screen.update(5,0,0,lines);assert(strstr(lines[2],"missing"));
    display_bus.imu[0x1e]=3;display_bus.axis(0x2c,16384);
    display_bus.field(139264,131072,131072);
    screen.update(5,0,100,lines);assert(screen.level_ok && strstr(lines[2],"Roll"));screen.draw(lcd,5);
    display_bus.axis(0x28,-8192);display_bus.axis(0x2c,14189);
    screen.update(5,1,200,lines);assert(near(screen.pitch,0) && strstr(lines[3],"Relative"));
    screen.update(5,2,300,lines);assert(near(screen.pitch,30,0.1f));screen.draw(lcd,5);
    screen.update(6,0,400,lines);assert(!screen.compass_ok && strstr(lines[2],"Hold flat"));
    display_bus.axis(0x28,0);display_bus.axis(0x2c,16384);display_bus.field(139264,131072,131072);
    screen.update(6,0,500,lines);assert(screen.compass_ok && strstr(lines[2],"000.0"));screen.draw(lcd,6);
    screen.update(6,1,600,lines);assert(screen.calibration.collecting && !screen.compass_ok);
    screen.update(6,1,700,lines);assert(screen.calibration.collecting); // Insufficient coverage: keep collecting.
    screen.update(6,2,800,lines);assert(!screen.calibration.collecting);
    display_bus.missing_mag=true;screen.update(6,0,900,lines);assert(!screen.compass_ok);
    const unsigned clears=lcd.clears,before=lcd.drawn;
    screen.draw(lcd,6);assert(lcd.clears==clears+1 && lcd.drawn==before+2); // Remove stale pointer.
#else
    const unsigned before=display_bus.calls;
    screen.update(5,1,100,lines);assert(strstr(lines[2],"disabled"));
    screen.update(6,1,200,lines);assert(strstr(lines[2],"disabled"));
    screen.draw(lcd,6);assert(before==display_bus.calls && !screen.compass_ok);
#endif
    puts("PASS: IMU/magnetometer registers, signed and 18-bit decode, independent recovery, stale data, level, compass, calibration");
}
