#include "power_path_model.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
struct Fake {
    bool mcp=true,ina=true,pd=true,fail_write=false;
    uint8_t regs[11]{},inputs=0xff;
    std::array<std::array<uint8_t,3>,64> monitor{};
    uint8_t pdregs[256]{};
    std::vector<std::array<unsigned,3>> writes;
    std::vector<unsigned> pdreads;
    Fake() {regs[0]=0xff;word(0x3e,0x5449);word(0x0b,3);}
    void word(unsigned reg,uint16_t n) {monitor[reg][0]=n>>8;monitor[reg][1]=n;}
    unsigned word(unsigned reg) {return (unsigned(monitor[reg][0])<<8)|monitor[reg][1];}
    uint8_t gpio() const {return (inputs&regs[0])|(regs[10]&~regs[0]);}
    static bool read(void *u,uint8_t address,uint8_t reg,uint8_t *out,size_t n) {
        auto &f=*static_cast<Fake *>(u);
        if(address==SK_POWER_MCP_ADDRESS) {
            if(!f.mcp)return false;
            assert(n==1 && reg<11);
            *out=reg==9?f.gpio():f.regs[reg];
            if(reg==8 || reg==9)f.regs[7]=0;
            return true;
        }
        if(address==SK_POWER_INA_ADDRESS) {
            if(!f.ina)return false;
            assert(reg<64 && n<=3);memcpy(out,f.monitor[reg].data(),n);return true;
        }
        assert(address==SK_POWER_PD_ADDRESS);
        if(!f.pd)return false;
        f.pdreads.push_back(reg);memcpy(out,f.pdregs+reg,n);return true;
    }
    static bool write(void *u,uint8_t address,uint8_t reg,const uint8_t *p,size_t n) {
        auto &f=*static_cast<Fake *>(u);
        f.writes.push_back({address,reg,p[0]});
        if(f.fail_write)return false;
        if(address==SK_POWER_MCP_ADDRESS) {
            if(!f.mcp)return false;
            assert(n==1 && reg<11);
            if(reg==10)assert(!(p[0]&0x40)); // Never drive KILL high.
            f.regs[reg]=p[0];return true;
        }
        assert(address==SK_POWER_INA_ADDRESS);
        if(!f.ina)return false;
        assert(n==2 && reg<64);
        memcpy(f.monitor[reg].data(),p,n);
        if(reg==0x0b)f.monitor[reg][1]|=3; // MEMSTAT + conversion complete.
        return true;
    }
    SkPowerPathIo io() {return {this,read,write};}
};
int main() {
    Fake missing;missing.mcp=missing.ina=missing.pd=false;
    SkPowerPathDriver absent(missing.io());
    assert(!absent.poll_gpio() && !absent.poll_meter());
    assert(!absent.usb("on") && !absent.cut());
    assert(!absent.set_limit(2000) && absent.state.limit_ma==2000 && !absent.state.limit_verified);
    assert(!absent.state.expander_connected && !absent.state.meter_valid);
    Fake f;SkPowerPathDriver d(f.io());
    assert(d.poll_gpio() && !d.state.usb_enabled && f.regs[0]==0xf7 && f.regs[10]==0);
    assert(f.regs[5]==4 && f.regs[2]==0xf7); // open-drain interrupt, all input changes.
    assert(d.usb("on") && d.state.usb_enabled);
    f.inputs&=~(1|4|32);assert(d.poll_gpio());
    assert(d.state.valid_inputs==5 && d.state.usb_attached);
    f.inputs&=~16;assert(d.poll_gpio() && d.state.usb_fault && d.state.usb_latched && !d.state.usb_enabled);
    assert(!d.usb("on") && !d.usb("clear"));
    f.inputs|=16;assert(d.usb("clear") && !d.state.usb_enabled && d.usb("on"));
    // Short fault captured by the expander must not disappear with a later high GPIO.
    f.regs[7]=16;f.regs[8]=f.inputs&~16;
    assert(d.poll_gpio() && !d.state.usb_fault && d.state.usb_latched && !d.state.usb_enabled);
    assert(d.usb("clear"));
    f.regs[7]=128;f.regs[8]=f.inputs&~128;
    assert(d.poll_gpio() && d.state.button_low);
    assert(d.poll_gpio() && !d.state.button_low);
    f.inputs&=~64;assert(d.poll_gpio() && d.state.kill_low && !d.usb("on"));f.inputs|=64;
    assert(d.usb("on"));
    // An expander reset invalidates output state, then reinitializes OFF, never ON.
    f.regs[0]=0xff;f.regs[10]=0;assert(!d.poll_gpio());
    assert(d.poll_gpio() && !d.state.usb_enabled && d.state.usb_latched);
    assert(d.usb("clear"));
    assert(d.poll_meter()); // completed simulated conversion
    assert(f.word(0)==0 && f.word(2)==8192 && f.word(1)==0xfb68 && f.word(0x0c)==0x7fff);
    assert(d.set_limit(2000) && f.word(0x0c)==4000 && d.state.limit_verified);
    assert(!d.set_limit(99) && !d.set_limit(5001) && d.state.limit_ma==2000);
    assert(d.set_limit(5000) && f.word(0x0c)==10000);
    assert(d.set_limit(100) && f.word(0x0c)==200);
    f.word(5,3840);f.word(7,1500);f.monitor[8]={0x01,0x5f,0x90}; // 90000 *0.2mW=18W
    assert(d.poll_meter() && d.state.mv==12000 && d.state.ma==1500 && d.state.mw==18000);
    f.word(7,0xff9c);assert(d.poll_meter() && d.state.ma==-100);
    f.word(0x0b,0x203);assert(!d.poll_meter() && !d.state.meter_valid); // overflow
    f.word(0x0b,2);assert(!d.poll_meter()); // trim failure
    f.word(0x0b,1);assert(!d.poll_meter()); // conversion not ready
    f.word(0x0b,3);
    f.word(2,4096);assert(!d.poll_meter() && !d.state.limit_verified); // standby monitor reset
    assert(d.poll_meter() && f.word(2)==8192 && f.word(0x0c)==200);
    f.fail_write=true;assert(!d.set_limit(2000) && !d.state.limit_verified);f.fail_write=false;
    assert(!d.poll_meter());assert(d.poll_meter() && f.word(0x0c)==4000);
    assert(d.set_limit(0) && f.word(0x0c)==0x7fff);
    assert(d.cut() && f.regs[0]==0xb7 && !(f.gpio()&64));
    Fake wrong;wrong.word(0x3e,0xffff);SkPowerPathDriver other(wrong.io());
    assert(!other.poll_meter() && wrong.writes.empty());
    // Passive PD reports the controller target and RDO without consuming alerts.
    Fake p;p.pdregs[0x2f]=0x25;p.pdregs[0x0e]=1;p.pdregs[0x10]=2;
    p.pdregs[0x21]=90;p.pdregs[0x29]=0x18;
    const uint32_t rdo=(3UL<<28)|(200UL<<10)|200;
    for(unsigned i=0;i<4;i++)p.pdregs[0x91+i]=rdo>>(8*i);
    SkPowerPdSnapshot pd;assert(sk_power_pd_observe(p.io(),&pd));
    assert(pd.connected && pd.attached && pd.rdo_valid && pd.set_mv==9000 && pd.rdo_ma==2000);
    for(unsigned reg:p.pdreads)assert(reg!=0x0b && reg!=0x0d && reg!=0x0f && reg!=0x16 && reg!=0x31 && reg!=0x33);
    assert(p.writes.empty());
    p.pdregs[0x94]|=4;assert(sk_power_pd_observe(p.io(),&pd) && !pd.rdo_valid); // mismatch
    p.pdregs[0x0e]=0;assert(sk_power_pd_observe(p.io(),&pd) && !pd.attached && !pd.rdo_valid && !pd.set_mv);
    p.pd=false;assert(!sk_power_pd_observe(p.io(),&pd) && !pd.connected);
    char rows[9][27]{};sk_power_path_format(absent.state,pd,rows);
    assert(strstr(rows[2],"unconnected") && strstr(rows[4],"unconnected") && strstr(rows[5],"unconnected"));
    assert(strstr(rows[7],"pending"));
    SkPowerPathSnapshot max{};max.expander_connected=max.meter_valid=max.limit_verified=true;
    max.mv=85000;max.ma=-32768;max.mw=3355443;max.valid_inputs=7;max.limit_ma=5000;
    sk_power_path_format(max,pd,rows);
    for(auto &row:rows)assert(strlen(row)<=26);
    assert(strstr(rows[2],"-32768mA") && !strcmp(rows[4],"Valid PD:1 DC:1 AUX:1") && strstr(rows[7],"armed"));
    puts("PASS: absent hardware, GPIO mapping, boot OFF, captured faults/button, open-drain KILL, ADC/calibration/reset, limits/readback, signed current/power, passive PD and bounded page");
}
