#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "solar_os_stusb4500.h"
static unsigned writes;
static bool fail;
static uint8_t regs[256];
static bool read_reg(void *u,uint8_t r,uint8_t *p,size_t n){
    (void)u;if(fail)return false;memcpy(p,regs+r,n);if(r==0x0b || r==0x0d || r==0x16)regs[r]=0;return true;
}
static bool write_reg(void *u,uint8_t r,const uint8_t *p,size_t n){
    (void)u;assert(r<0x95);if(fail)return false;++writes;memcpy(regs+r,p,n);return true;
}
static void put32(unsigned r,uint32_t v){for(unsigned i=0;i<4;++i)regs[r+i]=v>>(8*i);}
static void message(unsigned type,unsigned count){regs[0x0b]=2;regs[0x16]=4;regs[0x31]=type;regs[0x32]=count<<4;regs[0x30]=count*4;}
static solar_pd_power_result_t poll(solar_stusb4500_t *s,unsigned now){solar_pd_power_snapshot_t out;return solar_stusb4500_backend.poll(s,now,&out);}
static void caps(solar_stusb4500_t *s,unsigned now){message(1,3);put32(0x33,(100<<10)|300);put32(0x37,0xc0000000);put32(0x3b,(180<<10)|200);assert(poll(s,now)==SOLAR_PD_POWER_PENDING);}
int main(void){
    solar_stusb4500_t s;solar_stusb4500_io_t io={read_reg,write_reg,0};
    assert(!solar_stusb4500_init(&s,&io,20000,3000));regs[0x2f]=0x25;
    assert(solar_stusb4500_init(&s,&io,20000,3000));regs[0x0e]=1;regs[0x10]=2;
    caps(&s,10);assert(s.source.count==2 && s.source.profiles[1].mv==9000);
    unsigned before=writes;assert(!solar_stusb4500_backend.request(&s,(solar_pd_power_profile_t){20000,1000},20));assert(writes==before);
    assert(solar_stusb4500_backend.request(&s,(solar_pd_power_profile_t){9000,1000},20));
    assert(regs[0x70]==2 && regs[0x51]==13 && regs[0x1a]==0x26);
    put32(0x91,(3UL<<28)|(100<<10)|100);regs[0x29]=0x18;
    message(6,0);assert(poll(&s,30)==SOLAR_PD_POWER_PENDING && !s.source.contract_valid);
    caps(&s,35);message(3,0);assert(poll(&s,40)==SOLAR_PD_POWER_PENDING);
    message(6,0);assert(poll(&s,50)==SOLAR_PD_POWER_ACCEPTED);
    assert(s.source.contract_valid && s.source.contract.mv==9000 && s.source.contract.ma==1000);
    // Cached RDO cannot complete a new request.
    assert(solar_stusb4500_backend.request(&s,(solar_pd_power_profile_t){9000,1000},100));
    assert(poll(&s,101)==SOLAR_PD_POWER_PENDING && !s.source.contract_valid);
    assert(poll(&s,3100)==SOLAR_PD_POWER_PENDING && !s.pending);
    assert(solar_stusb4500_backend.request(&s,(solar_pd_power_profile_t){9000,1000},3200));
    message(4,0);assert(poll(&s,3201)==SOLAR_PD_POWER_DENIED);
    regs[0x0e]=0;assert(poll(&s,3300)==SOLAR_PD_POWER_DETACHED && !s.source.count);
    fail=true;assert(poll(&s,3400)==SOLAR_PD_POWER_FAILED && !s.source.contract_valid);
    puts("PASS: STUSB4500 bounds, fixed-PDO mapping, volatile writes, fresh negotiation, stale RDO, timeout, rejection, detach and I2C failure");
}
