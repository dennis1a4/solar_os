#pragma once
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <algorithm>

namespace sk_motion {
// Register/scaling sources: ST LSM6DSOX DS12814 and MEMSIC MMC5983MA Rev A.
constexpr float degrees=57.295779513f;
struct Vector { float x=0,y=0,z=0; };
struct Sample {
    Vector acceleration, gyro, magnetic; // g, degrees/second, microtesla
    uint32_t imu_at=0,mag_at=0;
    bool imu_valid=false,mag_valid=false;
    bool imu_fresh(uint32_t now) const {return imu_valid && uint32_t(now-imu_at)<500;}
    bool mag_fresh(uint32_t now) const {return mag_valid && uint32_t(now-mag_at)<500;}
};
// Transport supplies read(address, register, buffer, length) and write(address,
// register, value). Each operation must own the shared I2C bus for its duration.
template<class Bus> class Sensors {
    Bus &bus;
    bool imu_ready=false,mag_ready=false;
    uint32_t imu_probe=0,mag_probe=0;
    bool imu_tried=false,mag_tried=false;
    static int16_t signed16(const uint8_t *p) {return int16_t(uint16_t(p[0])|(uint16_t(p[1])<<8));}
public:
    Sample sample;
    uint32_t errors=0;
    explicit Sensors(Bus &transport):bus(transport) {}
    void poll(uint32_t now) {
        uint8_t id=0,status=0,data[12];
        if(imu_ready && uint32_t(now-(sample.imu_valid?sample.imu_at:imu_probe))>=2000) {
            imu_ready=false;sample.imu_valid=false;
        }
        if(mag_ready && uint32_t(now-(sample.mag_valid?sample.mag_at:mag_probe))>=2000) {
            mag_ready=false;sample.mag_valid=false;
        }
        if(!imu_ready && (!imu_tried || uint32_t(now-imu_probe)>=2000)) {
            imu_probe=now;imu_tried=true;
            // U17 SA0 is grounded: 0x6a. BDU and register auto-increment.
            imu_ready=bus.read(0x6a,0x0f,&id,1) && id==0x6c &&
                bus.write(0x6a,0x12,0x44) && bus.write(0x6a,0x10,0x30) &&
                bus.write(0x6a,0x11,0x30); // 52 Hz, +/-2 g and +/-250 dps
            if(!imu_ready) {sample.imu_valid=false;++errors;}
        }
        if(!mag_ready && (!mag_tried || uint32_t(now-mag_probe)>=2000)) {
            mag_probe=now;mag_tried=true;
            mag_ready=bus.read(0x30,0x2f,&id,1) && id==0x30 &&
                bus.write(0x30,0x0b,0) && bus.write(0x30,0x0a,0) &&
                bus.write(0x30,0x09,0x20) && bus.write(0x30,0x0b,0x8b);
            // Auto set/reset, 8 ms conversion, continuous 20 Hz, SET each sample.
            if(!mag_ready) {sample.mag_valid=false;++errors;}
        }
        if(imu_ready && uint32_t(now-imu_probe)>=100) {
            if(!bus.read(0x6a,0x1e,&status,1)) imu_ready=false;
            else if((status&3)==3) {
                imu_ready=bus.read(0x6a,0x22,data,12);
                if(imu_ready) {
                    sample.gyro={signed16(data)*0.00875f,signed16(data+2)*0.00875f,signed16(data+4)*0.00875f};
                    sample.acceleration={signed16(data+6)*0.000061f,signed16(data+8)*0.000061f,signed16(data+10)*0.000061f};
                    sample.imu_at=now;sample.imu_valid=true;
                }
            }
            if(!imu_ready) {sample.imu_valid=false;++errors;imu_probe=now;}
        }
        if(mag_ready && uint32_t(now-mag_probe)>=100) {
            if(!bus.read(0x30,0x08,&status,1)) mag_ready=false;
            else if(status&1) {
                mag_ready=bus.read(0x30,0,data,7) && bus.write(0x30,0x08,1);
                if(mag_ready) {
                    float values[3];
                    for(unsigned i=0;i<3;++i) {
                        const int32_t raw=(uint32_t(data[2*i])<<10)|(uint32_t(data[2*i+1])<<2)|((data[6]>>(6-2*i))&3);
                        values[i]=(raw-131072)*(100.0f/16384.0f);
                    }
                    sample.magnetic={values[0],values[1],values[2]};
                    sample.mag_at=now;sample.mag_valid=true;
                }
            }
            if(!mag_ready) {sample.mag_valid=false;++errors;mag_probe=now;}
        }
    }
};
inline bool level(const Vector &g,float &roll,float &pitch) {
    const float length=sqrtf(g.x*g.x+g.y*g.y+g.z*g.z);
    if(!isfinite(length) || length<0.85f || length>1.15f)return false;
    roll=atan2f(g.y,g.z)*degrees;
    pitch=atan2f(-g.x,sqrtf(g.y*g.y+g.z*g.z))*degrees;
    return true;
}
inline float wrap(float angle) {angle=fmodf(angle,360);return angle==0?0:angle<0?angle+360:angle;}
struct Calibration {
    Vector low,high,offset,scale{1,1,1};
    bool collecting=false,valid=false;
    unsigned count=0;
    void start() {collecting=true;count=0;}
    void add(Vector m) {
        if(!collecting)return;
        if(!count)low=high=m;
        else {
            low={std::min(low.x,m.x),std::min(low.y,m.y),std::min(low.z,m.z)};
            high={std::max(high.x,m.x),std::max(high.y,m.y),std::max(high.z,m.z)};
        }
        ++count;
    }
    bool finish() {
        if(count<50 || high.x-low.x<20 || high.y-low.y<20 || high.z-low.z<20)return false;
        const Vector radius={(high.x-low.x)/2,(high.y-low.y)/2,(high.z-low.z)/2};
        const float mean=(radius.x+radius.y+radius.z)/3;
        offset={(high.x+low.x)/2,(high.y+low.y)/2,(high.z+low.z)/2};
        scale={mean/radius.x,mean/radius.y,mean/radius.z};
        collecting=false;valid=true;return true;
    }
    Vector apply(Vector m) const {return {(m.x-offset.x)*scale.x,(m.y-offset.y)*scale.y,(m.z-offset.z)*scale.z};}
};
// Flat compass: X is sensor forward and Y is sensor right. Caller must gate
// on level/stationary data; no unverified inter-sensor axis alignment is assumed.
inline bool heading(Vector m,float &angle) {
    const float horizontal=sqrtf(m.x*m.x+m.y*m.y);
    const float total=sqrtf(horizontal*horizontal+m.z*m.z);
    if(!isfinite(total) || horizontal<5 || total<15 || total>100)return false;
    angle=wrap(atan2f(-m.y,m.x)*degrees);return true;
}
}
