#include "solar_os_stusb4500.h"
#include <string.h>
/* Register/protocol reference: ST's usb-c/STUSB4500 firmware and UM2650. */
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void pack(uint32_t v,uint8_t *p){for(unsigned i=0;i<4;++i)p[i]=v>>(i*8);}
static bool rd(solar_stusb4500_t *s,uint8_t r,uint8_t *p,size_t n){return s->io.read(s->io.user,r,p,n);}
static bool wr(solar_stusb4500_t *s,uint8_t r,const uint8_t *p,size_t n){return s->io.write(s->io.user,r,p,n);}
static void invalidate(solar_stusb4500_t *s){memset(&s->source,0,sizeof(s->source));s->raw_count=0;s->pending=s->fresh_caps=s->accepted=s->ps_ready=false;}
bool solar_stusb4500_init(solar_stusb4500_t *s,const solar_stusb4500_io_t *io,uint16_t mv,uint16_t ma){
    if(!s || !io || !io->read || !io->write || mv<5000 || mv>20000 || !ma || ma>5000)return false;
    memset(s,0,sizeof(*s));s->io=*io;s->max_mv=mv;s->max_ma=ma;
    uint8_t id;
    if(!rd(s,0x2f,&id,1) || (id!=0x25 && id!=0x21))return false;
    // Unmask receive/attachment/hard-reset events; caller supplies alert servicing.
    uint8_t mask=0x3d;
    s->initialized=wr(s,0x0c,&mask,1);return s->initialized;
}
static solar_pd_power_result_t poll(void *user,uint32_t now,solar_pd_power_snapshot_t *out){
    solar_stusb4500_t *s=user;
    if(s->pending && (uint32_t)(now-s->began)>=3000){s->pending=false;s->fresh_caps=s->accepted=s->ps_ready=false;s->source.contract_valid=false;}
    uint8_t alert,port,transition,monitor,header[2],prt;
    if(!s->initialized || !rd(s,0x0b,&alert,1) || !rd(s,0x0d,&transition,1) || !rd(s,0x0e,&port,1) || !rd(s,0x10,&monitor,1))goto failed;
    if(!(port&1) || (alert&0x80) || (transition&1)){invalidate(s);if(!(port&1)){*out=s->source;return SOLAR_PD_POWER_DETACHED;}}
    s->source.attached=true;
    if(!(monitor&2))s->source.contract_valid=false;
    if(alert&2){
        if(!rd(s,0x16,&prt,1))goto failed;
        if(prt&4){
            if(!rd(s,0x31,header,2))goto failed;
            unsigned count=(header[1]>>4)&7,type=header[0]&31;
            if(header[1]&0x80)goto failed;
            if(count && type==1){
                uint8_t n,data[28],again[2];
                if(!rd(s,0x30,&n,1) || n!=count*4 || !rd(s,0x33,data,n) || !rd(s,0x31,again,2) || memcmp(again,header,2))goto failed;
                s->raw_count=count;s->source.count=0;s->source.contract_valid=false;
                for(unsigned i=0;i<count;++i){
                    uint32_t p=le32(data+i*4);s->raw_pdos[i]=p;
                    unsigned mv=((p>>10)&1023)*50,ma=(p&1023)*10;
                    if(!(p>>30) && mv>=5000 && mv<=s->max_mv && ma){
                        solar_pd_power_profile_t *q=&s->source.profiles[s->source.count++];q->mv=mv;q->ma=ma<s->max_ma?ma:s->max_ma;
                    }
                }
                s->fresh_caps=true;s->accepted=s->ps_ready=false;
            }else if(!count){
                if(type==3 && s->fresh_caps)s->accepted=true;
                if(type==6 && s->accepted)s->ps_ready=true;
                if(type==4 || type==12){s->pending=false;s->source.contract_valid=false;*out=s->source;return SOLAR_PD_POWER_DENIED;}
            }
        }
    }
    if(s->ps_ready && (monitor&2)){
        uint8_t bytes[4],state;
        if(!rd(s,0x91,bytes,4) || !rd(s,0x29,&state,1))goto failed;
        uint32_t rdo=le32(bytes);unsigned index=(rdo>>28)&7,ma=((rdo>>10)&1023)*10;
        if(state==0x18 && index && index<=s->raw_count && !(rdo&(1UL<<26))){
            uint32_t p=s->raw_pdos[index-1];unsigned mv=((p>>10)&1023)*50;
            if(!(p>>30) && mv>=5000 && mv<=s->max_mv && ma && ma<=s->max_ma && ma<=(p&1023)*10){
                s->source.contract=(solar_pd_power_profile_t){mv,ma};s->source.contract_valid=true;
                if(s->pending){s->pending=false;*out=s->source;return SOLAR_PD_POWER_ACCEPTED;}
            }
        }
    }
    *out=s->source;return SOLAR_PD_POWER_PENDING;
failed:
    invalidate(s);*out=s->source;return SOLAR_PD_POWER_FAILED;
}
static bool request(void *user,solar_pd_power_profile_t p,uint32_t now){
    (void)now;solar_stusb4500_t *s=user;
    if(!s->initialized || s->pending || !s->source.attached || p.mv<5000 || p.mv>s->max_mv || p.mv%50 || !p.ma || p.ma>s->max_ma || p.ma%10)return false;
    bool offered=false;for(unsigned i=0;i<s->source.count;++i)if(s->source.profiles[i].mv==p.mv && s->source.profiles[i].ma>=p.ma)offered=true;
    if(!offered)return false;
    uint8_t data[8],discard;
    // Clear old receive indications before starting a new negotiation epoch.
    if(!rd(s,0x0b,&discard,1) || !rd(s,0x16,&discard,1))return false;
    pack((100UL<<10)|((s->max_ma<500?s->max_ma:500)/10),data);
    pack(((uint32_t)(p.mv/50)<<10)|(p.ma/10),data+4);
    uint8_t count=p.mv==5000?1:2;
    if(p.mv==5000)pack((100UL<<10)|(p.ma/10),data);
    uint8_t header[2]={13,0},command=0x26;
    s->fresh_caps=s->accepted=s->ps_ready=false;s->source.contract_valid=false;
    if(!wr(s,0x85,data,count*4) || !wr(s,0x70,&count,1) || !wr(s,0x51,header,2) || !wr(s,0x1a,&command,1)){invalidate(s);return false;}
    s->requested=p;s->pending=true;s->began=now;return true;
}
const solar_pd_power_backend_t solar_stusb4500_backend={request,poll};
