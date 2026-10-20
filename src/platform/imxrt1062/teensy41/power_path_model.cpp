#include "power_path_model.h"
#if SK_POWER_PATH
#include <stdio.h>
#include <string.h>
// MCP23008: GP3 is EN; GP6 is never driven high (wired-OR ALERT_N/KILL_N).
static constexpr uint8_t usb_en=1U<<3, fault_n=1U<<4, sink_n=1U<<5, kill_n=1U<<6;
bool SkPowerPathDriver::read8(uint8_t reg,uint8_t &v) {return io.read(io.user,SK_POWER_MCP_ADDRESS,reg,&v,1);}
bool SkPowerPathDriver::write8(uint8_t reg,uint8_t v) {return io.write(io.user,SK_POWER_MCP_ADDRESS,reg,&v,1);}
bool SkPowerPathDriver::read16(uint8_t reg,uint16_t &v) {
    uint8_t b[2];if(!io.read(io.user,SK_POWER_INA_ADDRESS,reg,b,2))return false;
    v=(uint16_t(b[0])<<8)|b[1];return true;
}
bool SkPowerPathDriver::write16(uint8_t reg,uint16_t v) {
    const uint8_t b[]={uint8_t(v>>8),uint8_t(v)};return io.write(io.user,SK_POWER_INA_ADDRESS,reg,b,2);
}
bool SkPowerPathDriver::configure_expander() {
    // Disable interrupts during setup; initialize the latch BEFORE direction.
    // GP6 remains an input, allowing the INA237 to pull KILL low independently.
    uint8_t dir,latch,polarity,config;
    if(!write8(2,0) || !write8(10,0) || !write8(0,0xf7) ||
       !write8(1,0) || !write8(5,4) || !write8(6,0x87) ||
       !write8(4,0) || !write8(2,0xf7) || !read8(0,dir) ||
       !read8(10,latch) || !read8(1,polarity) || !read8(5,config) ||
       dir!=0xf7 || latch!=0 || polarity!=0 || config!=4) return false;
    state.usb_enabled=false;
    expander_initialized=true;return true;
}
bool SkPowerPathDriver::poll_gpio() {
    uint8_t gpio,dir,latch,flags,capture;
    if((!expander_initialized && !configure_expander()) || !read8(0,dir) ||
       !read8(10,latch) || dir!=0xf7 || bool(latch&usb_en)!=state.usb_enabled ||
       !read8(7,flags) || !read8(8,capture) || !read8(9,gpio)) {
        // A reset/disconnect makes EN unknown. Never restore an old ON command.
        state.expander_connected=false;state.usb_latched=true;state.control_error=true;
        expander_initialized=false;return false;
    }
    state.expander_connected=true;
    state.valid_inputs=(~gpio)&7;
    state.usb_fault=!(gpio&fault_n);state.usb_attached=!(gpio&sink_n);
    state.kill_low=!(gpio&kill_n);state.button_low=!(gpio&128) || ((flags&128) && !(capture&128));
    if(state.usb_fault || ((flags&fault_n) && !(capture&fault_n))) {
        state.usb_latched=true;
        if(!write8(10,0)) {state.control_error=true;state.expander_connected=false;expander_initialized=false;return false;}
        state.usb_enabled=false;
    }
    state.control_error=false;return true;
}
bool SkPowerPathDriver::usb(const char *op) {
    if(!poll_gpio())return false;
    if(!strcmp(op,"status"))return true;
    if(!strcmp(op,"clear")) {if(state.usb_fault)return false;state.usb_latched=false;return true;}
    bool on=!strcmp(op,"on");
    if(!on && strcmp(op,"off"))return false;
    if(on && (state.usb_latched || state.usb_fault || state.kill_low))return false;
    if(!write8(10,on?usb_en:0)) {state.control_error=true;state.expander_connected=false;expander_initialized=false;return false;}
    state.usb_enabled=on;
    return poll_gpio() && (!on || state.usb_enabled);
}
bool SkPowerPathDriver::cut() {
    // Still in task context, before disabling interrupts: I2C needs IRQs.
    // A low latch followed by direction change emulates an open-drain output.
    if(!usb("off") || !write8(10,0) || !write8(0,0xb7))return false;
    uint8_t gpio;
    return read8(9,gpio) && !(gpio&kill_n);
}
bool SkPowerPathDriver::apply_limit() {
    // R13=10 mOhm, ADCRANGE=0: 1 mA -> 10 uV -> two 5-uV counts.
    const uint16_t threshold=state.limit_ma?uint16_t(state.limit_ma*2U):0x7fff;
    uint16_t actual;
    state.limit_verified=write16(0x0c,threshold) && read16(0x0c,actual) && actual==threshold;
    return state.limit_verified;
}
bool SkPowerPathDriver::set_limit(uint16_t ma) {
    if(ma && (ma<100 || ma>SK_POWER_MAX_CURRENT_MA || ma>16383))return false;
    state.limit_ma=ma;
    state.limit_verified=false;
    // Remember desired limit while absent; never claim it is armed until readback.
    return meter_initialized && apply_limit();
}
bool SkPowerPathDriver::configure_meter() {
    uint16_t manufacturer,config,calibration,adc,diag;
    if(!read16(0x3e,manufacturer) || manufacturer!=0x5449)return false;
    // No chip reset: the standby-powered monitor may already protect the rail.
    // Transparent, active-low alert. Conversion-ready MUST NOT drive KILL.
    // 1mA current LSB, SHUNT_CAL=819.2e6 * .001A * .010ohm = 8192.
    if(!write16(0,0) || !write16(2,8192) || !write16(0x0b,0) ||
       !apply_limit() || !write16(1,0xfb68) || !read16(0,config) ||
       !read16(2,calibration) || !read16(1,adc) || !read16(0x0b,diag) ||
       config!=0 || calibration!=8192 || adc!=0xfb68 || (diag&0xf000))return false;
    meter_initialized=true;return true;
}
bool SkPowerPathDriver::poll_meter() {
    uint16_t v,current,diag,threshold,config,calibration,adc;
    uint8_t power[3];
    state.meter_valid=false;
    if((!meter_initialized && !configure_meter()) || !read16(0,config) || config!=0 ||
       !read16(2,calibration) || calibration!=8192 || !read16(1,adc) || adc!=0xfb68 ||
       !read16(0x0c,threshold) || !read16(5,v) || !read16(7,current) ||
       !io.read(io.user,SK_POWER_INA_ADDRESS,8,power,3) || !read16(0x0b,diag) || (diag&0xf000)) {
        state.meter_connected=false;state.limit_verified=false;meter_initialized=false;return false;
    }
    state.meter_connected=true;
    state.limit_verified=threshold==(state.limit_ma?state.limit_ma*2U:0x7fff);
    if(!state.limit_verified) {meter_initialized=false;return false;}
    if(!(diag&1) || (diag&0x200) || !(diag&2))return false;
    state.mv=(uint32_t(v)*25+4)/8;
    state.ma=int16_t(current); // signed, 1mA/LSB; reverse current is preserved.
    state.mw=((uint32_t(power[0])<<16)|(uint32_t(power[1])<<8)|power[2])/5;
    state.meter_valid=true;return true;
}
bool sk_power_pd_observe(const SkPowerPathIo &io,SkPowerPdSnapshot *out) {
    *out={};
    auto rd=[&](uint8_t reg,uint8_t *b,size_t n){return io.read(io.user,SK_POWER_PD_ADDRESS,reg,b,n);};
    uint8_t id,port,monitor,target,pe,rdo[4],again[4],end_port,end_pe;
    if(!rd(0x2f,&id,1) || (id!=0x21 && id!=0x25) || !rd(0x0e,&port,1) ||
       !rd(0x10,&monitor,1) || !rd(0x21,&target,1) || !rd(0x29,&pe,1) ||
       !rd(0x91,rdo,4) || !rd(0x91,again,4) || !rd(0x0e,&end_port,1) || !rd(0x29,&end_pe,1))return false;
    out->connected=true;out->attached=(port&1) && (end_port&1);
    out->ready=out->attached && (monitor&2) && pe==0x18 && end_pe==0x18 && !memcmp(rdo,again,4);
    if(!out->attached)return true;
    // VSEL_PDO is the controller's requested monitoring target, NOT an ADC value.
    out->set_mv=uint16_t(target)*100;
    const uint32_t raw=uint32_t(rdo[0])|(uint32_t(rdo[1])<<8)|(uint32_t(rdo[2])<<16)|(uint32_t(rdo[3])<<24);
    out->rdo_valid=out->ready && ((raw>>28)&7) && !(raw&(1UL<<26)) && out->set_mv>=5000 && out->set_mv<=20000;
    if(out->rdo_valid)out->rdo_ma=((raw>>10)&1023)*10;
    if(!out->rdo_ma)out->rdo_valid=false;
    return true;
}
void sk_power_path_format(const SkPowerPathSnapshot &s,const SkPowerPdSnapshot &pd,char lines[9][27]) {
    strcpy(lines[1],"Power path");
    if(s.meter_valid) {
        snprintf(lines[2],27,"Bus %u.%03uV %dmA",unsigned(s.mv/1000)%1000,unsigned(s.mv%1000),int(int16_t(s.ma)));
        snprintf(lines[3],27,"Power %lu.%03luW",(unsigned long)(s.mw/1000),(unsigned long)(s.mw%1000));
    } else {
        strcpy(lines[2],s.meter_connected?"Meter: readings invalid":"Meter: unconnected");
        strcpy(lines[3],"Power -- W");
    }
    if(s.expander_connected)snprintf(lines[4],27,"Valid PD:%u DC:%u AUX:%u",!!(s.valid_inputs&1),!!(s.valid_inputs&2),!!(s.valid_inputs&4));
    else strcpy(lines[4],"Inputs: unconnected");
    if(!pd.connected)strcpy(lines[5],"PD: unconnected");
    else if(!pd.attached)strcpy(lines[5],"PD: detached");
    else if(pd.rdo_valid)snprintf(lines[5],27,"PD set %u.%uV %umA",pd.set_mv/1000,pd.set_mv%1000/100,pd.rdo_ma);
    else snprintf(lines[5],27,"PD set %u.%uV (waiting)",pd.set_mv/1000,pd.set_mv%1000/100);
    if(!s.expander_connected)strcpy(lines[6],"USB host: unconnected");
    else snprintf(lines[6],27,"USB %s %s",s.usb_latched?"FAULT":s.usb_enabled?"ON":"OFF",s.usb_attached?"attached":"detached");
    if(s.limit_ma)snprintf(lines[7],27,"Max %umA %s",s.limit_ma,s.limit_verified?"armed":"pending");
    else strcpy(lines[7],s.limit_verified?"Max: off":"Max: unknown");
}
#endif
