#pragma once
#include "motion_sensors.h"
#ifndef SK_MOTION
#define SK_MOTION 0
#endif

// Owned by the small-monitor task. Input IRQ/task paths only queue button bits.
struct SmallMotion {
    struct Transport {
        bool read(uint8_t a,uint8_t reg,uint8_t *out,size_t n) {
            return sk_i2c_transfer(1,a,&reg,1,out,n)==ESP_OK;
        }
        bool write(uint8_t a,uint8_t reg,uint8_t value) {
            const uint8_t data[]={reg,value};
            return sk_i2c_transfer(1,a,data,2,nullptr,0)==ESP_OK;
        }
    } bus;
    sk_motion::Sensors<Transport> sensors{bus};
    sk_motion::Calibration calibration;
    float roll=0,pitch=0,zero_roll=0,zero_pitch=0,bearing=0;
    bool level_ok=false,compass_ok=false;
    bool calibration_rejected=false;
    uint32_t calibration_at=0;

    void update(unsigned page,uint8_t buttons,uint32_t now,char lines[9][27]) {
        strcpy(lines[1],page==5?"Digital level":"Compass - magnetic");
        strcpy(lines[8],page==5?"<> Page A Zero B Reset":"<> Page A Cal B Cancel");
#if SK_MOTION
        sensors.poll(now);
        const auto &s=sensors.sample;
        float r=0,p=0;
        level_ok=s.imu_fresh(now) && sk_motion::level(s.acceleration,r,p);
        if(page==5) {
            if(buttons&2)zero_roll=zero_pitch=0;
            if((buttons&1) && level_ok) {zero_roll=r;zero_pitch=p;}
            roll=remainderf(r-zero_roll,360);pitch=p-zero_pitch;
            if(level_ok)snprintf(lines[2],27,"Roll %+.1f Pitch %+.1f",double(roll),double(pitch));
            else strcpy(lines[2],s.imu_fresh(now)?"Keep device still":"IMU missing / stale");
            strcpy(lines[3],zero_roll || zero_pitch?"Relative zero":"Absolute level");
            return;
        }
        if(buttons&2) {calibration.collecting=false;calibration_rejected=false;}
        if(buttons&1) {
            if(calibration.collecting)calibration_rejected=!calibration.finish();
            else {calibration.start();calibration_rejected=false;}
        }
        if(s.mag_fresh(now) && s.mag_at!=calibration_at) {
            calibration.add(s.magnetic);calibration_at=s.mag_at;
        }
        // The flat compass does not combine differently mounted sensor axes.
        const float lateral=s.acceleration.x*s.acceleration.x+s.acceleration.y*s.acceleration.y;
        const bool flat=level_ok && lateral<0.067f;
        compass_ok=flat && s.mag_fresh(now) && !calibration.collecting &&
            sk_motion::heading(calibration.apply(s.magnetic),bearing);
        if(!s.mag_fresh(now))strcpy(lines[2],"Magnetometer missing/stale");
        else if(calibration.collecting) {
            snprintf(lines[2],27,"Rotate all axes: %u",calibration.count);
        }
        else if(!s.imu_fresh(now))strcpy(lines[2],"IMU missing / stale");
        else if(!flat)strcpy(lines[2],"Hold flat and still");
        else if(!compass_ok)strcpy(lines[2],"Field invalid - move away");
        else {
            const char *directions[]={"N","NE","E","SE","S","SW","W","NW"};
            snprintf(lines[2],27,"%05.1f deg %s",double(bearing),directions[unsigned((bearing+22.5f)/45)%8]);
        }
        if(calibration.collecting)strcpy(lines[3],calibration_rejected?"Need wider 3-axis rotation":"A Save (50+ samples)");
        else strcpy(lines[3],calibration.valid?"Calibrated (this boot)":"Uncalibrated - A to cal");
#else
        (void)buttons;(void)now;
        strcpy(lines[2],"Sensors disabled in build");
#endif
    }
    template<class Display> void draw(Display &d,unsigned page) const {
        // Reserved rows 4..7, separate from the dashboard text cache.
        d.fillRect(0,56,160,54,ST7735_BLACK);
        const bool ok=page==5?level_ok:compass_ok;
        if(!ok)return;
        d.drawCircle(80,83,23,ST7735_WHITE);
        d.drawFastHLine(52,83,57,ST7735_WHITE);
        d.drawFastVLine(80,57,53,ST7735_WHITE);
        if(page==5) {
            const int x=80+int(std::max(-20.0f,std::min(20.0f,roll))*0.9f);
            const int y=83+int(std::max(-20.0f,std::min(20.0f,pitch))*0.9f);
            d.fillCircle(x,y,4,fabsf(roll)<0.5f && fabsf(pitch)<0.5f?ST7735_GREEN:ST7735_YELLOW);
        } else {
            const float radians=bearing/sk_motion::degrees;
            const int x=80-int(sinf(radians)*21), y=83-int(cosf(radians)*21);
            d.drawLine(80,83,x,y,ST7735_RED);
            d.fillCircle(x,y,3,ST7735_RED);
        }
    }
};
