#pragma once
#include <stdint.h>
#include <stddef.h>
extern "C" {
#include "solar_os_midi_codec.h"
}
// SMR1: header, then little-endian elapsed milliseconds + length/status/data1/data2.
// Transport-independent; deliberately distinct from Standard MIDI Files (.mid).
struct MidiEvent { uint32_t ms; solar_os_midi_message_t message; };
inline void midi_pack(const MidiEvent &e,uint8_t out[8]) {
    for(unsigned i=0;i<4;++i)out[i]=e.ms>>(8*i);
    out[4]=e.message.length;out[5]=e.message.status;out[6]=e.message.data1;out[7]=e.message.data2;
}
inline bool midi_unpack(const uint8_t in[8],MidiEvent &e) {
    e.ms=uint32_t(in[0])|(uint32_t(in[1])<<8)|(uint32_t(in[2])<<16)|(uint32_t(in[3])<<24);
    e.message={in[5],in[6],in[7],in[4]};
    return e.message.length && solar_os_midi_message_valid(&e.message) && e.message.status!=0xf7;
}
