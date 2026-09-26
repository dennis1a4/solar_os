#if SK_AUDIO_PLAYER
// Keep the large decoder in cached program flash, freeing an ITCM bank for
// runtime allocations. This affects declarations/definition only in this TU.
#define mp3dec_decode_frame __attribute__((section(".flashmem"))) mp3dec_decode_frame
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#endif
