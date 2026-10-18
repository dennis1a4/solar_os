#pragma once
#include <stdint.h>
#include <stddef.h>

// Units are millivolts at the probe, after the selected input divider.
// Hardware and demo producers share this model; no RTOS/display dependencies.
struct DmmModel {
    enum Mode { Live, Minimum, Maximum } mode=Live;
    enum { History=201, WindowMs=10000 };
    struct Point { uint32_t ms; int32_t mv; } history[History]{};
    unsigned next=0,count=0,range=5;
    uint32_t samples=0,missed=0,last_ms=0;
    int32_t live=0,minimum=0,maximum=0,held_value=0;
    bool valid=false,hold=false,overload=false,held_overload=false,min_overload=false,max_overload=false;

    int32_t selected() const { return mode==Minimum?minimum:mode==Maximum?maximum:live; }
    int32_t displayed() const { return hold?held_value:selected(); }
    bool selected_overload() const { return mode==Minimum?min_overload:mode==Maximum?max_overload:overload; }
    bool displayed_overload() const { return hold?held_overload:selected_overload(); }
    void set_mode(Mode value) { mode=value; hold=false; }
    void toggle_hold() {
        if(!valid)return;
        if(!hold) {held_value=selected();held_overload=selected_overload();}
        hold=!hold;
    }
    void reset_extrema() { minimum=maximum=live; min_overload=max_overload=overload; }
    void set_range(unsigned value) {
        if((value!=5 && value!=50) || range==value)return;
        *this=DmmModel{};range=value;
    }
    void sample(uint32_t ms,int32_t mv,bool clipped=false) {
        if(valid && uint32_t(ms-last_ms)>75) missed+=(uint32_t(ms-last_ms)/50)-1;
        live=mv;last_ms=ms;overload=clipped;
        if(!valid) {minimum=maximum=mv;min_overload=max_overload=clipped;valid=true;}
        if(mv<minimum) {minimum=mv;min_overload=clipped;}
        if(mv>maximum) {maximum=mv;max_overload=clipped;}
        ++samples;
        history[next]={ms,mv};next=(next+1)%History;if(count<History)++count;
    }
    // Debounced NES pressed edges, using A,B,Select,Start,Right,Left,Up,Down.
    bool buttons(uint8_t pressed) {
        if(pressed&8)return false;
        if(pressed&64)set_range(5);
        else if(pressed&128)set_range(50);
        if(pressed&1)toggle_hold();
        if(pressed&2)set_mode(Mode((unsigned(mode)+1)%3));
        if(pressed&16)set_mode(mode==Maximum?Live:Maximum);
        if(pressed&32)set_mode(mode==Minimum?Live:Minimum);
        if(pressed&4)reset_extrema();
        return true;
    }
    const Point &point(unsigned index) const { return history[(next+History-count+index)%History]; }
};

inline int32_t dmm_scale(unsigned raw,unsigned full_scale_mv) {
    return int32_t((uint64_t(raw)*full_scale_mv+511)/1023);
}
