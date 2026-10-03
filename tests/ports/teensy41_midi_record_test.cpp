#include "midi_record.h"
#include <cassert>
#include <cstdio>
int main(){
    uint8_t bytes[8];MidiEvent in{0x12345678,{0x91,60,100,3}},out{};
    midi_pack(in,bytes);assert(bytes[0]==0x78 && bytes[4]==3);
    assert(midi_unpack(bytes,out) && out.ms==in.ms && out.message.status==0x91);
    bytes[4]=0;assert(!midi_unpack(bytes,out));bytes[4]=3;bytes[6]=128;assert(!midi_unpack(bytes,out));
    solar_os_midi_decoder_t decoder{};solar_os_midi_message_t message{};
    assert(solar_os_midi_decode_byte(&decoder,0x90,&message)==SOLAR_OS_MIDI_DECODE_NONE);
    solar_os_midi_decode_byte(&decoder,60,&message);
    assert(solar_os_midi_decode_byte(&decoder,0xf8,&message)==SOLAR_OS_MIDI_DECODE_MESSAGE && message.status==0xf8);
    assert(solar_os_midi_decode_byte(&decoder,100,&message)==SOLAR_OS_MIDI_DECODE_MESSAGE && message.data1==60);
    solar_os_midi_decode_byte(&decoder,61,&message);
    assert(solar_os_midi_decode_byte(&decoder,0,&message)==SOLAR_OS_MIDI_DECODE_MESSAGE && message.data1==61);
    puts("PASS: MIDI timestamp round-trip, malformed messages, real-time interleaving and running status");
}
