#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Called with USB interrupts excluded. Repeat is generated only when consumed,
// never accumulated in the press queue. No allocation or clock dependencies.
class TeensyKeyboardInput {
public:
    static constexpr uint32_t delay_ms=400, interval_ms=33;
    struct Sequence { uint8_t bytes[8]{}; uint8_t size=0; };
    uint32_t presses=0, repeats=0, releases=0, disconnects=0, dropped=0;
    uint32_t generation=0;
    uint8_t modifiers=0, leds=0;
    uint8_t repeat_key() const { return candidate; }
    size_t queued() const { return (head+capacity-tail)%capacity; }
    void press(uint8_t key,uint32_t now) {
        if(key<4 || key>=0xe0 || down(key))return;
        held[key/8]|=uint8_t(1u<<(key%8)); ++presses;
        Sequence seq=encode(key,modifiers,leds);
        candidate=repeatable(key)?key:0; deadline=now+delay_ms;
        if(!seq.size)return;
        size_t next=(head+1)%capacity;
        if(next==tail) { ++dropped;candidate=0;return; }
        pending[head]=seq;head=next;
    }
    void release(uint8_t key) {
        if(down(key))++releases;
        held[key/8]&=uint8_t(~(1u<<(key%8)));
        if(candidate==key)candidate=0;
    }
    // A report, rather than the library's remembered state, owns boot-keyboard
    // reconciliation. This also recovers the first key after reconnect.
    void boot_report(const uint8_t *report,uint32_t now) {
        modifiers=report[0];
        for(unsigned i=2;i<8;++i)if(report[i]>0 && report[i]<4) { boundary();return; }
        for(unsigned key=4;key<0xe0;++key) {
            bool found=false;
            for(unsigned i=2;i<8;++i)found|=report[i]==key;
            if(down(key) && !found)release(key);
        }
        for(unsigned i=2;i<8;++i)press(report[i],now);
    }
    void boundary() {
        candidate=0;head=tail=0;active={};offset=0;++generation;
        // Retain held bits: switching apps must not re-arm a held key.
    }
    void disconnect() {
        boundary();std::memset(held,0,sizeof(held));modifiers=0;leds=0;++disconnects;
    }
    int read(uint32_t now) {
        if(offset<active.size)return active.bytes[offset++];
        active={};offset=0;
        if(tail!=head) { active=pending[tail];tail=(tail+1)%capacity; }
        else if(candidate && down(candidate) && int32_t(now-deadline)>=0) {
            deadline=now+interval_ms; // Do not catch up after a blocked consumer.
            // Ctrl/Alt/GUI chords are single-shot. Shift remains repeatable.
            if(!(modifiers&0xdd))active=encode(candidate,modifiers,leds);
            if(active.size)++repeats;
        }
        return active.size?active.bytes[offset++]:-1;
    }
    bool in_sequence() const { return offset<active.size; }
    static Sequence encode(uint8_t key,uint8_t mods,uint8_t locks) {
        Sequence out;
        auto text=[&](const char *s) { while(*s && out.size<sizeof(out.bytes))out.bytes[out.size++]=*s++; };
        const bool shift=mods&0x22, ctrl=mods&0x11;
        char c=0;
        if(key>=4 && key<=29)c=(shift!=bool(locks&2)?'A':'a')+key-4;
        else if(key>=30 && key<=39)c=(shift?"!@#$%^&*()":"1234567890")[key-30];
        else if(key>=45 && key<=56)c=(shift?"_+{}|~:\"~<>?":"-=[]\\#;'`,./")[key-45];
        else switch(key) {
        case 40:case 88:c='\r';break;case 41:c=27;break;case 42:c=8;break;
        case 43:c='\t';break;case 44:c=' ';break;
        case 84:c='/';break;case 85:c='*';break;case 86:c='-';break;case 87:c='+';break;
        case 100:c=shift?'|':'\\';break;
        }
        // Numeric keypad obeys NumLock; otherwise use navigation equivalents.
        if(key>=89 && key<=99) {
            if(locks&1)c="1234567890."[key-89];
            else { static constexpr uint8_t nav[]={77,81,78,80,0,79,74,82,75,73,76};key=nav[key-89]; }
        }
        if(c) {
            if(ctrl && c>='@' && c<='~')c&=31;
            out.bytes[0]=uint8_t(c);out.size=1;return out;
        }
        const unsigned modifier=1+(shift?1:0)+(mods&0x44?2:0)+(ctrl?4:0);
        char suffix=0;
        switch(key) { case 79:suffix='C';break;case 80:suffix='D';break;case 81:suffix='B';break;
            case 82:suffix='A';break;case 74:suffix='H';break;case 77:suffix='F';break; }
        if(suffix) {
            text("\033[");if(modifier!=1) { text("1;");out.bytes[out.size++]='0'+modifier; }
            out.bytes[out.size++]=suffix;return out;
        }
        if(key>=58 && key<=61 && modifier==1) {
            text("\033O");out.bytes[out.size++]='P'+key-58;return out;
        }
        unsigned number=0;
        if(key>=58 && key<=69) { static constexpr unsigned fn[]={11,12,13,14,15,17,18,19,20,21,23,24};number=fn[key-58]; }
        else switch(key) { case 73:number=2;break;case 76:number=3;break;case 75:number=5;break;case 78:number=6;break; }
        if(number) {
            text("\033[");if(number>=10)out.bytes[out.size++]='0'+number/10;
            out.bytes[out.size++]='0'+number%10;
            if(modifier!=1) { out.bytes[out.size++]=';';out.bytes[out.size++]='0'+modifier; }
            out.bytes[out.size++]='~';
        }
        return out;
    }
private:
    static constexpr size_t capacity=48;
    Sequence pending[capacity]{},active{};
    size_t head=0,tail=0;
    uint8_t offset=0,candidate=0,held[32]{};
    uint32_t deadline=0;
    bool down(uint8_t key) const { return held[key/8]&(1u<<(key%8)); }
    static bool repeatable(uint8_t key) {
        return (key>=4 && key<=39) || key==42 || (key>=44 && key<=56) ||
            (key>=73 && key<=87) || (key>=89 && key<=100);
    }
};
